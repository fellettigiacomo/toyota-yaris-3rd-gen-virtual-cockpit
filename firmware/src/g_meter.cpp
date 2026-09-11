#include "g_meter.h"
#include "imu.h"

#include <Arduino.h>
#include <cmath>

// The board is glued to the dash in whatever orientation it happens to fit,
// so the chip's axes mean nothing on their own. Two references turn them
// into vehicle axes, and this project can derive BOTH from data it already
// has, with no setup screen and nothing for the driver to do:
//
//   "up"      -- the accelerometer at rest measures specific force, which at
//                a standstill is exactly +1 g along whatever chip axis points
//                at the sky. CAN says when the car is stopped (speed_kph),
//                so the stationary samples to average are free.
//   "forward" -- during straight-line acceleration the horizontal part of
//                that same reading points along the car. CAN says when the
//                car is accelerating and how hard (d(speed_kph)/dt), which
//                both gates the samples and resolves the sign, so braking
//                teaches the axis just as well as accelerating does.
//
// From there: horizontal = measured - up*(measured . up), and the vehicle
// frame is the usual right-handed x=forward, y=right, z=down, so
// right = forward x up.
//
// The forward fit is a weighted vector average rather than a single-shot
// capture -- one hard launch converges it in a couple of seconds, and every
// subsequent acceleration keeps refining it.
namespace GMeter {

namespace {

// --- "up" capture ---------------------------------------------------------
constexpr float kStationaryKph = 0.5f;
constexpr float kGravityMagMin = 0.90f; // reject samples that aren't just gravity
constexpr float kGravityMagMax = 1.10f;
constexpr float kGravityJitterG = 0.05f;   // how far a sample may stray and still count as "still"
constexpr uint32_t kGravitySettleMs = 800; // stationary and steady this long before "up" is trusted
constexpr float kGravityAlpha = 0.05f;     // slow: re-levels across a session without chasing bumps

// --- "forward" fit --------------------------------------------------------
constexpr float kLearnMinSpeedKph = 5.0f; // below this, speed quantization dominates d(speed)/dt
constexpr float kLearnMinDvdtG = 0.05f;   // ignore cruise; only real accel/braking carries direction
constexpr float kLearnStraightSlackG = 0.06f; // reject corners: |horizontal| must match |dv/dt|
// In g-seconds, not samples: weighting by elapsed time instead of by sample
// count keeps "converged" meaning the same thing whatever rate update() is
// called at. 0.2 is one 0.3g launch held for two thirds of a second, or a
// gentle 0.07g roll away from the lights for three.
constexpr float kLearnReadyWeight = 0.2f;
constexpr float kMaxDtSec = 0.2f; // a stalled tick must not dump a huge weight in at once

// --- smoothing ------------------------------------------------------------
constexpr float kDvdtAlpha = 0.15f; // d(speed)/dt is quantized at 0.01 km/h per CAN frame
constexpr float kOutAlpha = 0.25f;  // ~5 ticks to settle at the 30Hz UI rate

constexpr float kMsPerSecond = 1000.0f;
constexpr float kKphToMs = 1.0f / 3.6f;
constexpr float kGravityMs2 = 9.80665f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

Vec3 add(const Vec3 &a, const Vec3 &b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(const Vec3 &a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float norm(const Vec3 &a) { return std::sqrt(dot(a, a)); }
Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalized(const Vec3 &a, bool &ok) {
    float n = norm(a);
    ok = n > 1e-4f;
    return ok ? scale(a, 1.0f / n) : Vec3{};
}

Vec3 g_up;            // unit, chip axes: the direction the sky is in
Vec3 g_gravityEma;    // running average of stationary samples, pre-normalization
bool g_haveUp = false;
uint32_t g_steadySinceMs = 0;
bool g_steady = false;

Vec3 g_fwdSum;        // weighted sum of observed forward directions (not normalized)
float g_learnWeight = 0.0f;

float g_lastSpeedKph = 0.0f;
uint32_t g_lastSpeedMs = 0;
bool g_haveSpeed = false;
float g_dvdtG = 0.0f;

float g_longG = 0.0f;
float g_latG = 0.0f;

Reading g_reading;

// d(speed)/dt in g, smoothed. Returns false until there are two samples to
// difference.
// Returns the elapsed time it consumed, 0 when there was nothing to do.
float updateDvdt(const VehicleState &state, uint32_t nowMs) {
    if (!g_haveSpeed) {
        g_lastSpeedKph = state.speed_kph;
        g_lastSpeedMs = nowMs;
        g_haveSpeed = true;
        return 0.0f;
    }
    uint32_t dtMs = nowMs - g_lastSpeedMs;
    if (dtMs == 0) return 0.0f; // same millisecond; keep the previous estimate
    float dt = static_cast<float>(dtMs) / kMsPerSecond;
    if (dt > kMaxDtSec) dt = kMaxDtSec;
    float dv = (state.speed_kph - g_lastSpeedKph) * kKphToMs;
    g_lastSpeedKph = state.speed_kph;
    g_lastSpeedMs = nowMs;

    float instantG = (dv / dt) / kGravityMs2;
    g_dvdtG += kDvdtAlpha * (instantG - g_dvdtG);
    return dt;
}

// Averages stationary samples into "up". Three gates, and all three are
// load-bearing:
//
//   stopped         -- CAN says the wheels aren't turning.
//   magnitude ~ 1 g -- the reading is gravity and nothing else.
//   held still      -- the vector hasn't moved for kGravitySettleMs.
//
// The last one is the one the simulator caught. Braking to a halt trips the
// first two the instant speed hits zero, while the car is still pitched
// forward and still decelerating: a reading that is 1.09 g and tilted, which
// sails through a magnitude gate and quietly tilts "level" by a few degrees.
// Requiring the vector to sit still for the better part of a second rejects
// that, along with door slams and someone leaning on a wing, because every
// one of them moves the vector.
void updateUp(const Vec3 &sample, const VehicleState &state, uint32_t nowMs) {
    bool stopped = state.speed_kph < kStationaryKph;
    float mag = norm(sample);
    bool looksLikeGravity = mag > kGravityMagMin && mag < kGravityMagMax;

    if (!stopped || !looksLikeGravity) {
        g_steady = false;
        return;
    }
    if (!g_steady) {
        g_steady = true;
        g_steadySinceMs = nowMs;
        g_gravityEma = sample; // seed the window on its own first sample
        return;
    }
    // Any real movement restarts the window rather than averaging into it.
    if (norm(add(sample, scale(g_gravityEma, -1.0f))) > kGravityJitterG) {
        g_steadySinceMs = nowMs;
        g_gravityEma = sample;
        return;
    }
    g_gravityEma = add(g_gravityEma, scale(add(sample, scale(g_gravityEma, -1.0f)), kGravityAlpha));

    if (nowMs - g_steadySinceMs < kGravitySettleMs) return;
    bool ok = false;
    Vec3 up = normalized(g_gravityEma, ok);
    if (!ok) return;
    g_up = up;
    g_haveUp = true;
}

// Folds one straight-line acceleration sample into the forward fit.
void updateForward(const Vec3 &horizontal, const VehicleState &state, float dtSec) {
    if (dtSec <= 0.0f) return;
    if (state.speed_kph < kLearnMinSpeedKph) return;
    float pull = std::fabs(g_dvdtG);
    if (pull < kLearnMinDvdtG) return;

    // Straightness gate: in a straight line the horizontal vector IS the
    // longitudinal one, so its magnitude should match what CAN reports. A
    // corner makes |horizontal| much larger than |dv/dt| and would drag the
    // fitted axis sideways.
    float mag = norm(horizontal);
    if (mag > pull + kLearnStraightSlackG) return;

    bool ok = false;
    Vec3 dir = normalized(horizontal, ok);
    if (!ok) return;
    // Braking points the horizontal vector backwards, so flip it by the sign
    // CAN gives -- decelerating teaches the same axis as accelerating.
    if (g_dvdtG < 0.0f) dir = scale(dir, -1.0f);

    g_fwdSum = add(g_fwdSum, scale(dir, pull * dtSec));
    g_learnWeight += pull * dtSec;
}

} // namespace

void begin() {
    g_up = Vec3{};
    g_gravityEma = Vec3{};
    g_haveUp = false;
    g_steady = false;
    g_fwdSum = Vec3{};
    g_learnWeight = 0.0f;
    g_haveSpeed = false;
    g_dvdtG = 0.0f;
    g_longG = 0.0f;
    g_latG = 0.0f;
    g_reading = Reading{};
    Imu::begin();
}

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    Imu::Sample s = Imu::read();

    if (!Imu::present()) {
        g_reading.phase = Phase::NoSensor;
        return;
    }
    if (!s.valid) return; // chip present but no fresh sample: hold the last reading

    Vec3 sample{s.ax, s.ay, s.az};

    float dtSec = updateDvdt(state, nowMs);
    updateUp(sample, state, nowMs);

    if (!g_haveUp) {
        g_reading.phase = Phase::Zeroing;
        g_reading.longG = 0.0f;
        g_reading.latG = 0.0f;
        g_reading.magG = 0.0f;
        return;
    }

    // Specific force minus its vertical part. At rest this is zero; under
    // any real acceleration it is that acceleration, in the horizontal
    // plane, which is the only plane a g-meter cares about.
    Vec3 horizontal = add(sample, scale(g_up, -dot(sample, g_up)));

    updateForward(horizontal, state, dtSec);

    g_reading.learnPct = 100.0f * g_learnWeight / kLearnReadyWeight;
    if (g_reading.learnPct > 100.0f) g_reading.learnPct = 100.0f;

    if (g_learnWeight < kLearnReadyWeight) {
        g_reading.phase = Phase::Learning;
        g_reading.longG = 0.0f;
        g_reading.latG = 0.0f;
        g_reading.magG = 0.0f;
        return;
    }

    // Forward, re-levelled against "up" so a fit gathered while the car
    // pitched under braking doesn't leave the axis tilted out of the plane.
    bool ok = false;
    Vec3 fwd = normalized(add(g_fwdSum, scale(g_up, -dot(g_fwdSum, g_up))), ok);
    if (!ok) {
        g_reading.phase = Phase::Learning;
        return;
    }
    // x=forward, y=right, z=down (right-handed) => y = z x x = forward x up.
    Vec3 right = cross(fwd, g_up);

    float rawLong = dot(horizontal, fwd);
    float rawLat = dot(horizontal, right);
    g_longG += kOutAlpha * (rawLong - g_longG);
    g_latG += kOutAlpha * (rawLat - g_latG);

    g_reading.phase = Phase::Ready;
    g_reading.longG = g_longG;
    g_reading.latG = g_latG;
    g_reading.magG = std::sqrt(g_longG * g_longG + g_latG * g_latG);

    // Peaks track the smoothed value on purpose: an unfiltered one-sample
    // spike off a pothole is not a g the car ever pulled.
    if (g_longG > g_reading.peakAccelG) g_reading.peakAccelG = g_longG;
    if (-g_longG > g_reading.peakBrakeG) g_reading.peakBrakeG = -g_longG;
    if (g_latG > g_reading.peakRightG) g_reading.peakRightG = g_latG;
    if (-g_latG > g_reading.peakLeftG) g_reading.peakLeftG = -g_latG;
}

Reading get() { return g_reading; }

} // namespace GMeter
