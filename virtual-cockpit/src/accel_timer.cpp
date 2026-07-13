#include "accel_timer.h"

#include <Arduino.h>

// A run starts the moment speed leaves standstill in Drive, and ends (or
// aborts) at 100 km/h, at a gear change out of D, or back at standstill.
// Only the most recently crossed threshold's result is shown -- 50 while a
// run is still climbing toward 100, then 100 once that's crossed too -- and
// it's held on screen for kHoldMs after the crossing, then cockpit_ui falls
// back to the gear letter on its own (see getDisplay()).
namespace AccelTimer {

namespace {

constexpr float kStoppedKph = 1.0f; // below this, (re)arm for the next run
constexpr uint32_t kHoldMs = 7000;  // how long a result stays on screen

bool g_running = false;
uint32_t g_startMs = 0;
bool g_got50 = false;
bool g_got100 = false;

int g_shownThreshold = 0; // 0 = nothing latched yet this power-on
float g_shownSeconds = 0.0f;
uint32_t g_shownAtMs = 0;

void latch(int thresholdKph, uint32_t nowMs) {
    g_shownThreshold = thresholdKph;
    g_shownSeconds = (nowMs - g_startMs) / 1000.0f;
    g_shownAtMs = nowMs;
}

} // namespace

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    bool forwardGear = (state.gear == Gear::D);

    if (state.speed_kph < kStoppedKph) {
        // Standstill (or crawling) -- (re)arm. A run that never reached 50
        // before coming back to a stop is simply dropped, nothing to show.
        g_running = false;
        g_got50 = false;
        g_got100 = false;
        return;
    }

    if (!g_running) {
        if (forwardGear) {
            g_running = true;
            g_startMs = nowMs;
            g_got50 = false;
            g_got100 = false;
        }
        return;
    }

    if (!forwardGear) {
        g_running = false; // shifted out of D mid-run -- abort, keep whatever result was already shown
        return;
    }

    if (!g_got50 && state.speed_kph >= 50.0f) {
        g_got50 = true;
        latch(50, nowMs);
    }
    if (!g_got100 && state.speed_kph >= 100.0f) {
        g_got100 = true;
        g_running = false; // nothing further to time
        latch(100, nowMs);
    }
}

Display getDisplay() {
    Display d;
    if (g_shownThreshold == 0) return d;
    if (millis() - g_shownAtMs > kHoldMs) return d;
    d.active = true;
    d.thresholdKph = g_shownThreshold;
    d.seconds = g_shownSeconds;
    return d;
}

} // namespace AccelTimer
