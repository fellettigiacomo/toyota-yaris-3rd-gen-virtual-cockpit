#pragma once

#include <lvgl.h>
#include "vehicle_state.h"

// Second screen: trip distance and the whole drive's PWR graph on the left
// of the cockpit's centre column, average and instantaneous consumption,
// range and fuel level on the right.
//
// All of it comes from HybridStats/PwrHistory, which integrate the
// already-decoded signals; this file does no arithmetic beyond smoothing,
// rounding and placement. All lv_* calls happen from the LVGL task, same
// rule as the other screens.
namespace EfficiencyUi {

void build(lv_obj_t *parent);
void update(const VehicleState &state);

} // namespace EfficiencyUi
