#include "accel_timer.h"

#include <Arduino.h>

// A run starts the moment speed leaves standstill in Drive -- and *only*
// from standstill: the timer has to be armed by an actual stop first, so
// picking up speed again mid-drive (or after a completed run) can't start a
// new one. It ends (or aborts) at 100 km/h, at a gear change out of D, or
// back at standstill.
// Only the most recently crossed threshold's result is shown -- 50 while a
// run is still climbing toward 100, then 100 once that's crossed too -- and
// it's held on screen for kHoldMs after the crossing, then cockpit_ui falls
// back to the gear letter on its own (see getDisplay()).
namespace AccelTimer {

namespace {

constexpr float kStoppedKph = 1.0f; // below this, (re)arm for the next run
constexpr uint32_t kHoldMs = 7000;  // how long a result stays on screen

// A crossing that took longer than this isn't a real acceleration figure --
// more likely stop-and-go traffic than a sprint -- so it's dropped instead
// of shown. Generous enough to cover unhurried everyday driving (this
// hybrid's own 0-100 spec is ~11.8s flat out) while rejecting multi-minute
// crawls. Each threshold has its own cap so a slow 0-50 split doesn't
// disqualify a 0-100 that still finishes in reasonable total time.
constexpr uint32_t kMaxReasonableMs50 = 15000;
constexpr uint32_t kMaxReasonableMs100 = 30000;

bool g_armed = false; // saw standstill since the last run -- a new run may start
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
        g_armed = true;
        g_got50 = false;
        g_got100 = false;
        return;
    }

    if (!g_running) {
        // Moving but not timing: only a stop can start the next run, so
        // cruising past 50/100 km/h (or powering on already under way)
        // stays untimed instead of latching a bogus fraction of a second.
        if (g_armed && forwardGear) {
            g_running = true;
            g_armed = false;
            g_startMs = nowMs;
            g_got50 = false;
            g_got100 = false;
        }
        return;
    }

    if (!forwardGear) {
        // Shifted out of D mid-run -- abort, keep whatever result was already
        // shown. Still disarmed: the next run has to start from a stop.
        g_running = false;
        return;
    }

    if (!g_got50 && state.speed_kph >= 50.0f) {
        g_got50 = true;
        if (nowMs - g_startMs <= kMaxReasonableMs50) {
            latch(50, nowMs);
        }
    }
    if (!g_got100 && state.speed_kph >= 100.0f) {
        g_got100 = true;
        g_running = false; // nothing further to time (and disarmed until the next stop)
        if (nowMs - g_startMs <= kMaxReasonableMs100) {
            latch(100, nowMs); // overwrites whatever 0-50 result (if any) is currently shown
        }
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
