#pragma once

#include <ctime>
#include <lvgl.h>
#include "vehicle_state.h"

// Builds and updates the 640x172 gauge cluster widget tree, matching the
// Cockpit.dc.html design spec. All lv_* calls happen from the LVGL task
// (see display_driver.cpp) -- build()/update() must only ever be called
// from there.
namespace CockpitUi {

// Constructs the static widget tree once, parented to `parent` (an
// lv_tileview tile). Call after lv_disp_drv_register().
void build(lv_obj_t *parent);

// Pushes a fresh VehicleState (and, separately, wall-clock time for the
// left-slot clock -- not part of VehicleState since it comes from the RTC,
// not CAN) into the widget tree. clockEpoch==0 means "RTC not set/valid",
// in which case the clock label is left blank rather than showing 1970.
void update(const VehicleState &state, time_t clockEpoch);

} // namespace CockpitUi
