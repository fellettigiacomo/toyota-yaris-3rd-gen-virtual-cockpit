#pragma once

#include "vehicle_state.h"

// Session-cumulative hybrid driving stats, accumulated from the already-decoded
// VehicleState signals (no new CAN work). "Session" == since power-on: the
// cluster is powered from the car's accessory rail, so power-on approximates
// the start of a drive, and everything resets naturally on the next start.
//
// update() is cheap (a few float adds) and must be called every UI tick,
// regardless of which screen is visible, so the numbers are correct whenever
// the efficiency screen is shown. getSnapshot() returns the derived values.
namespace HybridStats {

struct Snapshot {
    float distanceKm = 0.0f;     // total distance this session
    float evSharePct = 0.0f;     // share of distance driven purely electric (0..100)
    float regenSharePct = 0.0f;  // share of MOVING time spent regenerating (0..100)
    float avgSpeedKph = 0.0f;    // distance / moving-time (excludes standstill)
    float maxSpeedKph = 0.0f;    // peak speed seen this session
    float socNowPct = 0.0f;
    float socMinPct = 0.0f;
    float socMaxPct = 0.0f;
};

// Feed one VehicleState sample; integrates against wall-clock (millis()).
void update(const VehicleState &state);

Snapshot getSnapshot();

} // namespace HybridStats
