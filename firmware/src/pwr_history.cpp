#include "pwr_history.h"

#include <Arduino.h>

namespace PwrHistory {

namespace {

constexpr uint32_t kInitialBucketMs = 500;

Bucket g_buckets[kCapacity];
int g_size = 0;
uint32_t g_bucketMs = kInitialBucketMs;
uint32_t g_revision = 0;

// The open bucket accumulates time-weighted, so a bucket covering a mix of
// long and short UI ticks still averages what the driver actually did.
uint32_t g_lastMs = 0;
uint32_t g_openMs = 0;
float g_openPwrSum = 0.0f; // pct * ms
uint32_t g_openEvMs = 0;

uint8_t meanPct(float sum, uint32_t ms) {
    float v = sum / static_cast<float>(ms) + 0.5f;
    if (v < 0.0f) v = 0.0f;
    if (v > 100.0f) v = 100.0f;
    return static_cast<uint8_t>(v);
}

// Same scale as the cockpit's PWR gauge (raw~200 is a full bar), with the
// CHG side flattened to zero: the graph shows how hard the car is pushed.
float pwrPctOf(const VehicleState &state) {
    if (state.hsi_power <= 0) return 0.0f;
    float pct = static_cast<float>(state.hsi_power) * 0.5f;
    return pct > 100.0f ? 100.0f : pct;
}

// Electric as HybridStats counts it, plus anything regenerating -- charging
// is the pack's side of the ledger even if the engine happens to be spinning.
bool isElectric(const VehicleState &state) {
    return state.ev_drive || !state.ice_running || state.hsi_power < 0;
}

void compact() {
    for (int i = 0; i < kCapacity / 2; i++) {
        const Bucket &a = g_buckets[2 * i];
        const Bucket &b = g_buckets[2 * i + 1];
        g_buckets[i].pwrPct = static_cast<uint8_t>((a.pwrPct + b.pwrPct + 1) / 2);
        g_buckets[i].evPct = static_cast<uint8_t>((a.evPct + b.evPct + 1) / 2);
    }
    g_size = kCapacity / 2;
    // The open bucket keeps the time it has already gathered and simply runs
    // on to the new, doubled length.
    g_bucketMs *= 2;
}

} // namespace

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    if (g_lastMs == 0) {
        g_lastMs = nowMs;
        return; // first sample just seeds the clock
    }
    uint32_t dtMs = nowMs - g_lastMs;
    g_lastMs = nowMs;
    if (dtMs == 0 || dtMs > 1000) return; // ignore absurd gaps, as HybridStats does

    g_openMs += dtMs;
    g_openPwrSum += pwrPctOf(state) * static_cast<float>(dtMs);
    if (isElectric(state)) g_openEvMs += dtMs;

    if (g_openMs < g_bucketMs) return;

    Bucket b;
    b.pwrPct = meanPct(g_openPwrSum, g_openMs);
    b.evPct = meanPct(100.0f * static_cast<float>(g_openEvMs), g_openMs);
    g_buckets[g_size++] = b;
    g_openMs = 0;
    g_openPwrSum = 0.0f;
    g_openEvMs = 0;

    if (g_size == kCapacity) compact();
    g_revision++;
}

int size() {
    return g_size;
}

Bucket at(int i) {
    return g_buckets[i];
}

bool open(Bucket &out) {
    if (g_openMs == 0) return false;
    out.pwrPct = meanPct(g_openPwrSum, g_openMs);
    out.evPct = meanPct(100.0f * static_cast<float>(g_openEvMs), g_openMs);
    return true;
}

uint32_t revision() {
    return g_revision;
}

} // namespace PwrHistory
