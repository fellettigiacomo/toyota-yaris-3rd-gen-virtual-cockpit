#pragma once

#include <lvgl.h>
#include "vehicle_state.h"

// Third screen (reached by the BOOT button): session hybrid-efficiency stats
// -- EV vs engine share, distance, regen, average/max speed -- all derived
// from HybridStats (which integrates the already-decoded signals). All lv_*
// calls happen from the LVGL task, same rule as the other screens.
namespace EfficiencyUi {

void build(lv_obj_t *parent);
void update(const VehicleState &state);

} // namespace EfficiencyUi
