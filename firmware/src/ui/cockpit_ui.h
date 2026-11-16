#pragma once

#include <lvgl.h>
#include "vehicle_state.h"

// Builds and updates the 640x172 gauge cluster widget tree, matching the
// Cockpit.dc.html design spec. All lv_* calls happen from the LVGL task
// (see display_driver.cpp) -- build()/update() must only ever be called
// from there.
namespace CockpitUi {

// Constructs the static widget tree once, parented to `parent` (a plain
// full-screen container -- see AppUi::build()). Call after
// lv_disp_drv_register().
void build(lv_obj_t *parent);

// Pushes a fresh VehicleState into the widget tree.
void update(const VehicleState &state);

// Start-up gauge sweep, like a car's instrument check: both side gauges run
// 0 -> 100 -> 0 and then settle onto the live value. update() plays it out;
// AppUi starts it the moment the boot splash is gone.
void startSweep();
bool sweeping();

} // namespace CockpitUi
