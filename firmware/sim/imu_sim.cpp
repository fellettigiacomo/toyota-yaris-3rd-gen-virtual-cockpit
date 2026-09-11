#include "imu.h"
#include "can_decoder.h"

#include <Arduino.h>
#include <cmath>

// Desktop stand-in for src/imu.cpp -- there is no QMI8658 on a Mac. The
// simulator links this instead of the real driver (see sim/CMakeLists.txt);
// everything above the seam, g_meter.cpp's whole calibration included, is
// the firmware's own code running unmodified.
//
// Rather than hand the G-meter pre-aligned axes, this synthesizes what a
// real chip would have measured, and then rotates it into a deliberately
// awkward mounting orientation. So the simulator exercises the part most
// likely to be wrong: working out which way the board is facing.
//
//   longitudinal -- taken from the replayed drive log's own d(speed)/dt, so
//                   the dot moves with the same launches and stops the other
//                   screens show. (The firmware derives its calibration
//                   reference from that same signal, which is why the fit
//                   converges instantly here and will take a few seconds of
//                   real driving in the car.)
//   lateral      -- synthesized: the log carries no steering or yaw signal,
//                   so there is nothing real to replay. A slow weave, scaled
//                   by speed, stands in for cornering.
//   gravity      -- added last, in the vehicle frame, exactly as a real
//                   accelerometer would feel it.
namespace Imu {

namespace {

// Mounting: yaw 35 deg, pitch 20 deg, roll 10 deg away from the car's axes.
// Arbitrary on purpose -- nothing downstream is allowed to assume otherwise.
constexpr float kYawDeg = 35.0f;
constexpr float kPitchDeg = 20.0f;
constexpr float kRollDeg = 10.0f;

constexpr float kLatAmplitudeG = 0.45f;
constexpr float kLatPeriodSec = 11.0f;
constexpr float kLatFullEffectKph = 50.0f;

constexpr float kDvdtAlpha = 0.2f;
constexpr float kKphToMs = 1.0f / 3.6f;
constexpr float kGravityMs2 = 9.80665f;
constexpr float kPi = 3.14159265358979f;

float g_lastSpeedKph = 0.0f;
uint32_t g_lastMs = 0;
bool g_haveSpeed = false;
float g_longG = 0.0f;

// Rotates a vehicle-frame vector (x forward, y right, z down) into the
// chip's frame: Rz(yaw) * Ry(pitch) * Rx(roll), applied as the sensor's
// axes seen from the car.
void toSensorFrame(float vx, float vy, float vz, float &sx, float &sy, float &sz) {
    const float cy = std::cos(kYawDeg * kPi / 180.0f), sy_ = std::sin(kYawDeg * kPi / 180.0f);
    const float cp = std::cos(kPitchDeg * kPi / 180.0f), sp = std::sin(kPitchDeg * kPi / 180.0f);
    const float cr = std::cos(kRollDeg * kPi / 180.0f), sr = std::sin(kRollDeg * kPi / 180.0f);

    // Rz
    float x1 = cy * vx - sy_ * vy;
    float y1 = sy_ * vx + cy * vy;
    float z1 = vz;
    // Ry
    float x2 = cp * x1 + sp * z1;
    float y2 = y1;
    float z2 = -sp * x1 + cp * z1;
    // Rx
    sx = x2;
    sy = cr * y2 - sr * z2;
    sz = sr * y2 + cr * z2;
}

} // namespace

void begin() {
    g_haveSpeed = false;
    g_longG = 0.0f;
}

bool present() { return true; }

Sample read() {
    VehicleState state = CanDecoder::getSnapshot();
    uint32_t nowMs = millis();

    if (!g_haveSpeed) {
        g_haveSpeed = true;
        g_lastSpeedKph = state.speed_kph;
        g_lastMs = nowMs;
    } else if (nowMs != g_lastMs) {
        float dt = static_cast<float>(nowMs - g_lastMs) / 1000.0f;
        float dv = (state.speed_kph - g_lastSpeedKph) * kKphToMs;
        g_lastSpeedKph = state.speed_kph;
        g_lastMs = nowMs;
        float instantG = (dv / dt) / kGravityMs2;
        g_longG += kDvdtAlpha * (instantG - g_longG);
    }

    float speedFactor = state.speed_kph / kLatFullEffectKph;
    if (speedFactor > 1.0f) speedFactor = 1.0f;
    float t = static_cast<float>(nowMs) / 1000.0f;
    float latG = kLatAmplitudeG * std::sin(2.0f * kPi * t / kLatPeriodSec) * speedFactor;

    // Specific force in the vehicle frame: the car's own acceleration, minus
    // gravity. z is down, so gravity shows up as -1 g on that axis -- which
    // is why an accelerometer at rest reads +1 g pointing at the sky.
    Sample s;
    s.valid = true;
    toSensorFrame(g_longG, latG, -1.0f, s.ax, s.ay, s.az);
    return s;
}

} // namespace Imu
