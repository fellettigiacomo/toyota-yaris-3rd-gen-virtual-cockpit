#pragma once

#include "vehicle_state.h"

#include <lvgl.h>

// Fourth screen: a G-meter. A classic g-ball left of the cockpit's centre
// column (dot = the acceleration vector, up is accelerating, down is braking,
// sides are which way the car is turning), the current magnitude and the
// session's four directional peaks right of it.
//
// All the math -- and all the work of figuring out how the board is mounted
// -- is in g_meter.cpp; this file only draws what GMeter::get() reports.
// Same build()/update() contract as the other screens, both LVGL-task only.
namespace GMeterUi {

void build(lv_obj_t *parent);
void update(const VehicleState &state);

} // namespace GMeterUi
