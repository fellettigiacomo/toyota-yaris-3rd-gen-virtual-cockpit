#pragma once

#include "vehicle_state.h"

// 0-50 / 0-100 km/h acceleration timer, derived from the already-decoded
// VehicleState (speed_kph + gear, no new CAN work) -- same shape as
// hybrid_stats.h. update() is cheap and must be called every UI tick,
// regardless of which screen is visible, so a run started while another
// screen is shown isn't missed.
namespace AccelTimer {

struct Display {
    bool active = false;   // false => cockpit_ui shows the gear letter as usual
    int thresholdKph = 0;  // 50 or 100
    float seconds = 0.0f;  // elapsed time from standstill to thresholdKph
};

void update(const VehicleState &state);

// What cockpit_ui should show right now, if anything. active==false means
// "show the gear letter" -- no result pending or the hold window expired.
Display getDisplay();

} // namespace AccelTimer
