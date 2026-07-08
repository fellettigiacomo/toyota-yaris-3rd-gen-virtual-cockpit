#pragma once

#include "vehicle_state.h"

// Passive (listen-only) TWAI capture + DBC decode for the ~7 signals this
// cluster displays. See dbc/toyota_yaris_xp130_reversed.dbc for the ground
// truth this decoder implements.
//
// Reuses obd-capture's proven TWAI bring-up (TWAI_MODE_LISTEN_ONLY,
// TWAI_TIMING_CONFIG_500KBITS(), accept-all filter) but, unlike obd-capture,
// decodes straight into a VehicleState struct instead of retaining raw
// frames -- there's no SD logging in this project.
namespace CanDecoder {

// Installs the TWAI driver and spawns canRxTask pinned to CORE_CAN_RX at
// TASK_PRIO_CAN_RX. Must be called after Serial.begin().
void begin();

// Thread-safe snapshot of the latest decoded values, safe to call from the
// LVGL task at whatever rate it wants (UI_SYNC_INTERVAL_MS).
VehicleState getSnapshot();

struct CanDecoderStats {
    float frames_per_sec;
    uint32_t bus_error_count;
    uint32_t bus_off_count;
};

// Optional debug overlay data -- not shown in the shipped UI, but cheap to
// have on hand while bringing this up on the actual vehicle bus.
CanDecoderStats getStats();

} // namespace CanDecoder
