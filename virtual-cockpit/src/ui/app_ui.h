#pragma once

#include <ctime>
#include "vehicle_state.h"

// Thin orchestrator: owns the two full-screen containers and delegates each
// to its own module (CockpitUi, EnergyFlowUi), switching between them via
// the BOOT button (see screen_nav.h) instead of a touch swipe. Keeps
// display_driver.cpp free of screen-content knowledge, same separation it
// already had with CockpitUi before this existed. All lv_* calls happen
// from the LVGL task (see display_driver.cpp) -- build()/update() must only
// ever be called from there.
namespace AppUi {

// Constructs both screens. Call after lv_disp_drv_register().
void build();

// Pushes a fresh VehicleState/clock into the cockpit screen, and into the
// energy-flow screen only while it's the one actually shown (its animated
// arrows are too expensive to redraw unconditionally, see energy_flow_ui.cpp).
void update(const VehicleState &state, time_t clockEpoch);

} // namespace AppUi
