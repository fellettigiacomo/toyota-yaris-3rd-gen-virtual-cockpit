#pragma once

#include "vehicle_state.h"

#include <cstdint>

// Whole-session history of the PWR demand, for the efficiency screen's line
// graph. "Session" == since power-on, same as HybridStats.
//
// The history always spans the entire drive in a fixed kCapacity buckets:
// buckets start 0.5s long, and whenever the buffer fills, neighbouring pairs
// are averaged into one and the bucket length doubles. Memory and redraw cost
// stay constant however long the drive gets; only the time resolution drops.
//
// update() must run every UI tick regardless of the visible screen, like
// HybridStats::update().
namespace PwrHistory {

constexpr int kCapacity = 320;

struct Bucket {
    uint8_t pwrPct; // mean PWR demand, 0..100 on the cockpit gauge's scale (CHG counts as 0)
    uint8_t evPct;  // share of the bucket's time spent electric (CHG counts as electric)
};

void update(const VehicleState &state);

// Closed buckets, oldest first.
int size();
Bucket at(int i);

// The bucket still being filled, so the line's newest point moves live.
// Returns false until it has seen any time.
bool open(Bucket &out);

// Bumped whenever a bucket closes or the buffer is compacted.
uint32_t revision();

} // namespace PwrHistory
