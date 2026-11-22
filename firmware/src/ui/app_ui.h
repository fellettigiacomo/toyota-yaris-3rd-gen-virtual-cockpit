#pragma once

#include "vehicle_state.h"

// Thin orchestrator: owns the screens' containers and delegates each to its
// own module, switching between them via the BOOT button (see screen_nav.h)
// or a tap. Keeps display_driver.cpp free of screen-content knowledge. All
// lv_* calls happen from the LVGL task (see display_driver.cpp) --
// build()/update() must only ever be called from there.
namespace AppUi {

// Constructs every screen. Call after lv_disp_drv_register().
void build();

// Pushes a fresh VehicleState into the cockpit cluster, and into the other
// screens only while they are the one actually shown.
void update(const VehicleState &state);

} // namespace AppUi
