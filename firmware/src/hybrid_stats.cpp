#include "hybrid_stats.h"

#include <Arduino.h>

namespace HybridStats {

namespace {

// "Moving" hysteresis, same idea as the energy screen's stopped-latch: avoids
// counting jitter around standstill into the moving-time / distance totals.
constexpr float kMoveEnterKph = 3.0f;
constexpr float kMoveExitKph = 1.0f;
bool g_moving = false;

// "Decelerating / regenerating" -- same qualitative definition the energy-flow
// screen uses, so the two screens agree on what counts as regen.
constexpr float kDecelHsi = 5.0f;
constexpr int8_t kAccelDemandDecel = -8;

uint32_t g_lastMs = 0;

double g_distanceKm = 0.0;
double g_evDistKm = 0.0;
double g_movingTimeS = 0.0;
double g_regenTimeS = 0.0;
float g_maxSpeedKph = 0.0f;

float g_socNow = 0.0f;
float g_socMin = -1.0f; // sentinel: not yet seen
float g_socMax = -1.0f;

} // namespace

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    if (g_lastMs == 0) {
        g_lastMs = nowMs;
        return; // first sample just seeds the clock
    }
    float dtS = (nowMs - g_lastMs) / 1000.0f;
    g_lastMs = nowMs;
    if (dtS <= 0.0f || dtS > 1.0f) return; // ignore absurd gaps (first frame, pauses)

    // SOC range tracking (always, even at standstill). Guard against the
    // startup window before the first HV_BATTERY frame arrives, when
    // battery_soc_pct is still its 0 default -- a real HV pack never sits at
    // 0%, so treat 0 as "no reading yet" and don't let it latch the minimum.
    if (state.battery_soc_pct > 0.0f) {
        g_socNow = state.battery_soc_pct;
        if (g_socMin < 0.0f || state.battery_soc_pct < g_socMin) g_socMin = state.battery_soc_pct;
        if (g_socMax < 0.0f || state.battery_soc_pct > g_socMax) g_socMax = state.battery_soc_pct;
    }

    // Moving hysteresis.
    if (g_moving) {
        if (state.speed_kph < kMoveExitKph) g_moving = false;
    } else {
        if (state.speed_kph > kMoveEnterKph) g_moving = true;
    }
    if (!g_moving) return;

    if (state.speed_kph > g_maxSpeedKph) g_maxSpeedKph = state.speed_kph;

    float distKm = state.speed_kph * (dtS / 3600.0f);
    g_distanceKm += distKm;
    g_movingTimeS += dtS;

    // EV share by distance: count distance covered with the ICE off (pure EV
    // propulsion, incl. regen coasting). Distance with the engine spinning
    // goes to the ICE bucket implicitly (total - ev).
    if (state.ev_drive || !state.ice_running) {
        g_evDistKm += distKm;
    }

    bool decelerating = state.brake_pressed || state.accel_demand < kAccelDemandDecel ||
                        state.hsi_power <= -kDecelHsi;
    if (decelerating) g_regenTimeS += dtS;
}

Snapshot getSnapshot() {
    Snapshot s;
    s.distanceKm = static_cast<float>(g_distanceKm);
    s.evSharePct = (g_distanceKm > 0.01) ? static_cast<float>(100.0 * g_evDistKm / g_distanceKm) : 0.0f;
    s.regenSharePct = (g_movingTimeS > 0.5) ? static_cast<float>(100.0 * g_regenTimeS / g_movingTimeS) : 0.0f;
    s.avgSpeedKph = (g_movingTimeS > 0.5) ? static_cast<float>(g_distanceKm / (g_movingTimeS / 3600.0)) : 0.0f;
    s.maxSpeedKph = g_maxSpeedKph;
    s.socNowPct = g_socNow;
    s.socMinPct = (g_socMin < 0.0f) ? g_socNow : g_socMin;
    s.socMaxPct = (g_socMax < 0.0f) ? g_socNow : g_socMax;
    return s;
}

} // namespace HybridStats
