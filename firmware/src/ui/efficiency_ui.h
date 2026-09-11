#pragma once

#include <lvgl.h>
#include "vehicle_state.h"

// Third screen (reached by the BOOT button): the session's EV-vs-engine
// split, drawn as a full-bleed divide of the whole screen rather than as a
// bar sitting on it -- the seam between the two shares is the layout, and
// both percentages hang off it at the cockpit's own 120px hero size.
// Distance and average speed keep the bottom corners, one per side.
//
// All of it comes from HybridStats, which integrates the already-decoded
// signals; this file does no arithmetic beyond rounding and placement. All
// lv_* calls happen from the LVGL task, same rule as the other screens.
namespace EfficiencyUi {

void build(lv_obj_t *parent);
void update(const VehicleState &state);

} // namespace EfficiencyUi
