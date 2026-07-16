#pragma once

#include <cstdint>
#include <cstddef>
#include "can_capture.h"

// Fixed-size, lock-protected table of unique CAN IDs seen so far, with last
// payload and an exponential-moving-average frequency estimate. Written by
// canRxTask (short critical section, never blocks), read via a snapshot copy
// by displayTask so rendering never holds the lock for long.

struct CanIdEntry {
    uint32_t id;
    uint8_t dlc;
    uint8_t last_data[8];
    bool extended;
    bool rtr;
    uint32_t count;
    uint64_t last_seen_us;
    float hz;
};

namespace CanIdTable {

void init();

// Called from canRxTask for every received frame.
void update(const CanFrameRecord &rec);

// Copies up to maxEntries into out (sorted by ID), returns the number copied.
size_t snapshot(CanIdEntry *out, size_t maxEntries);

size_t size();

} // namespace CanIdTable
