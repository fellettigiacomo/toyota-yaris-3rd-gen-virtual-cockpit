// Headless renderer for the README's screenshots.
//
// Builds the real LVGL UI (src/ui/*, unmodified), drives it with a scripted
// VehicleState, and dumps the framebuffer the panel would have received. No
// SDL and no window, so it runs anywhere the simulator's toolchain builds --
// including CI containers with no display.
//
// It is deliberately NOT a mockup tool: every pixel comes from the same
// build()/update() the firmware calls, which is what makes these frames
// usable as documentation. It has caught real bugs, too -- the G-meter's
// gravity capture was tilted by braking-to-a-halt, and that showed up here
// before it ever reached a car.
//
// Timing note: the UI modules integrate against millis(), so the scenes below
// play out in real time rather than being fast-forwarded. The efficiency
// scene is the slow one (~2 minutes): its EV share is distance-weighted, so
// the only way to reach a given split with a plausible trip behind it is to
// actually drive the distance.

#include <lvgl.h>

#include "ui/app_ui.h"
#include "imu.h"
#include "g_meter.h"
#include "hybrid_stats.h"
#include "vehicle_state.h"

#include <Arduino.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

constexpr int kW = 640;
constexpr int kH = 172;

lv_disp_draw_buf_t g_drawBuf;
lv_disp_drv_t g_dispDrv;
lv_color_t *g_buf = nullptr;
uint16_t g_frame[kW * kH];
std::string g_outDir = ".";

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int fx = area->x1 + x, fy = area->y1 + y;
            if (fx < 0 || fx >= kW || fy < 0 || fy >= kH) continue;
            // LV_COLOR_16_SWAP=1 in include/lv_conf.h pre-swaps each pixel for
            // the QSPI panel's wire format -- undo it, same as the SDL port's
            // flush_cb does.
            uint16_t v = color_p[y * w + x].full;
            g_frame[fy * kW + fx] = static_cast<uint16_t>((v >> 8) | (v << 8));
        }
    }
    lv_disp_flush_ready(drv);
}

// Raw RGB888, one byte per channel, row-major. topng.py turns it into the
// .png -- writing PNG from here would mean a zlib dependency for something
// Python's standard library already does.
void dump(const char *name) {
    std::string path = g_outDir + "/" + name + ".rgb";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        std::printf("cannot write %s\n", path.c_str());
        return;
    }
    for (int i = 0; i < kW * kH; i++) {
        uint16_t v = g_frame[i];
        uint8_t rgb[3] = {static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31),
                          static_cast<uint8_t>(((v >> 5) & 0x3F) * 255 / 63),
                          static_cast<uint8_t>((v & 0x1F) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    std::printf("  wrote %s.rgb\n", name);
}

// --- scripted IMU ---------------------------------------------------------
// Stands in for src/imu.cpp (there is no QMI8658 here), and deliberately does
// not hand g_meter.cpp pre-aligned axes: it synthesizes what a real chip
// would have measured and rotates it through an arbitrary mounting, so the
// calibration has to do its real job to produce the frame below.
float g_scriptLongG = 0.0f;
float g_scriptLatG = 0.0f;

void toSensorFrame(float vx, float vy, float vz, float &sx, float &sy, float &sz) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    const float cosYaw = std::cos(35 * kDeg), sinYaw = std::sin(35 * kDeg);
    const float cosPitch = std::cos(20 * kDeg), sinPitch = std::sin(20 * kDeg);
    const float cosRoll = std::cos(10 * kDeg), sinRoll = std::sin(10 * kDeg);

    float x1 = cosYaw * vx - sinYaw * vy;
    float y1 = sinYaw * vx + cosYaw * vy;
    float z1 = vz;

    float x2 = cosPitch * x1 + sinPitch * z1;
    float y2 = y1;
    float z2 = -sinPitch * x1 + cosPitch * z1;

    sx = x2;
    sy = cosRoll * y2 - sinRoll * z2;
    sz = sinRoll * y2 + cosRoll * z2;
}

} // namespace

namespace Imu {
void begin() {}
bool present() { return true; }
Sample read() {
    Sample s;
    s.valid = true;
    // Specific force in vehicle axes (x forward, y right, z down): the car's
    // own acceleration minus gravity, which is why z reads -1 g at rest.
    toSensorFrame(g_scriptLongG, g_scriptLatG, -1.0f, s.ax, s.ay, s.az);
    return s;
}
} // namespace Imu

namespace {

int g_screen = 0; // 0 cockpit, 1 energy, 2 efficiency, 3 G-meter
VehicleState g_state;

void step(int ms) {
    lv_tick_inc(ms);
    AppUi::update(g_state);
    lv_timer_handler();
}

void runFor(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        step(16);
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
}

// Screens are cycled through the same BOOT-button path the cluster uses, so
// nothing here reaches around AppUi's own navigation. Press AND release have
// to outlast ScreenNav's 30ms debounce -- a release that ends early leaves
// the debouncer latched and every later press is swallowed.
void hold(bool held, int steps) {
    g_simButtonHeld = held;
    for (int i = 0; i < steps; i++) {
        step(20);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void gotoScreen(int target) {
    while (g_screen != target) {
        hold(true, 4);
        hold(false, 4);
        g_screen = (g_screen + 1) % 4;
    }
}

// --- 01 cockpit -----------------------------------------------------------
// Rendered first, and never from a standstill: AccelTimer arms itself at zero
// and would then latch a bogus 0-50 the instant speed jumps, replacing the
// gear letter with a timer readout.
void sceneCockpit() {
    std::printf("01_cockpit\n");
    g_state = VehicleState{};
    g_state.gear = Gear::D;
    g_state.ice_running = true;
    g_state.speed_kph = 64.0f;
    g_state.rpm = 1850;
    g_state.battery_soc_pct = 68.0f;
    g_state.hsi_power = 38; // 19% PWR once the display's halved scale is applied
    gotoScreen(0);
    runFor(400);
    dump("01_cockpit");
}

// --- 02 energy flow -------------------------------------------------------
// Hybrid drive: the engine feeding the wheels while the pack assists through
// the motor, which lights three of the four links and leaves one idle -- the
// mix that shows both states of a link in one frame.
void sceneEnergyFlow() {
    std::printf("02_energy_flow\n");
    g_state.speed_kph = 60.0f;
    g_state.gear = Gear::D;
    g_state.ice_running = true;
    g_state.ev_drive = false;
    g_state.brake_pressed = false;
    g_state.accel_demand = 40;
    g_state.hsi_power = 60;
    g_state.soc_trend_pct_per_s = -0.3f;
    gotoScreen(1);
    // Long enough for the travelling segments to be caught mid-run rather
    // than parked at a link's start.
    runFor(500);
    dump("02_energy_flow");
}

// --- 03 efficiency --------------------------------------------------------
// The EV share is distance-weighted, so it can only be steered by driving:
// cover ground on the motor, hand over to the engine, and capture when the
// split has fallen to something worth showing. The short way round gives a
// 100m trip, which reads as a bug rather than as a short drive.
void sceneEfficiency() {
    std::printf("03_efficiency (the slow one -- ~2 minutes of simulated driving)\n");
    gotoScreen(2);
    g_state.ice_running = false;
    g_state.ev_drive = true;
    g_state.speed_kph = 70.0f;
    g_state.hsi_power = 20;
    g_state.accel_demand = 20;

    uint32_t t0 = millis();
    for (;;) {
        float t = static_cast<float>(millis() - t0) / 1000.0f;
        bool onMotor = t < 75.0f;
        g_state.ev_drive = onMotor;
        g_state.ice_running = !onMotor;
        step(16);
        HybridStats::Snapshot s = HybridStats::getSnapshot();
        if (t > 76.0f && s.evSharePct <= 72.0f) {
            std::printf("  ev=%.0f%% dist=%.2fkm avg=%.0fkm/h\n", s.evSharePct, s.distanceKm,
                        s.avgSpeedKph);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    dump("03_efficiency");
}

// --- 04 G-meter -----------------------------------------------------------
// Plays a short drive so the calibration actually converges: a standstill for
// the level reference, a launch for the forward axis, then braking and both
// corners to put something in the peaks, ending trail-braking into a right.
void sceneGMeter() {
    std::printf("04_gmeter\n");
    gotoScreen(3);
    g_state = VehicleState{};
    g_state.gear = Gear::D;
    g_state.ice_running = true;
    g_state.battery_soc_pct = 68.0f;
    GMeter::begin(); // starts the level-capture window here, not at boot

    struct Leg {
        float seconds, longG, latG;
    };
    const Leg kLegs[] = {
        {1.6f, 0.00f, 0.00f},   // stopped: gravity only, level captured
        {2.8f, 0.07f, 0.00f},   // gentle roll away
        {4.0f, 0.30f, 0.00f},   // a real launch: the forward axis converges
        {1.0f, -0.45f, 0.00f},  // hard braking
        {1.0f, 0.00f, 0.55f},   // right-hander
        {1.0f, 0.00f, -0.62f},  // left-hander
        {1.4f, -0.28f, 0.34f},  // trail-braking into a right: the frame
    };

    float speedKph = 0.0f;
    uint32_t last = millis();
    for (const Leg &leg : kLegs) {
        uint32_t t0 = millis();
        while (millis() - t0 < static_cast<uint32_t>(leg.seconds * 1000.0f)) {
            uint32_t now = millis();
            float dt = static_cast<float>(now - last) / 1000.0f;
            last = now;
            g_scriptLongG = leg.longG;
            g_scriptLatG = leg.latG;
            // Speed is integrated from the scripted longitudinal g so CAN and
            // the IMU agree -- the calibration is built on them agreeing.
            speedKph += leg.longG * 9.80665f * dt * 3.6f;
            if (speedKph < 0.0f) speedKph = 0.0f;
            g_state.speed_kph = speedKph;
            step(16);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    GMeter::Reading r = GMeter::get();
    std::printf("  long=%+.2f lat=%+.2f mag=%.2f\n", r.longG, r.latG, r.magG);
    dump("04_gmeter");
}

} // namespace

int main(int argc, char **argv) {
    g_outDir = argc > 1 ? argv[1] : ".";

    lv_init();
    g_buf = static_cast<lv_color_t *>(malloc(kW * kH * sizeof(lv_color_t)));
    lv_disp_draw_buf_init(&g_drawBuf, g_buf, nullptr, kW * kH);
    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = kW;
    g_dispDrv.ver_res = kH;
    g_dispDrv.flush_cb = flushCb;
    g_dispDrv.draw_buf = &g_drawBuf;
    lv_disp_drv_register(&g_dispDrv);

    AppUi::build();

    sceneCockpit();
    sceneEnergyFlow();
    sceneEfficiency();
    sceneGMeter();

    std::printf("done -- run topng.py over the .rgb files\n");
    return 0;
}
