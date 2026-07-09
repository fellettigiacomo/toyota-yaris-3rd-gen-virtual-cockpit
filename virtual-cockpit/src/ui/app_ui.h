#pragma once

#include <ctime>
#include "vehicle_state.h"

// Thin orchestrator: owns the lv_tileview root and delegates each tile to
// its own module (CockpitUi, EnergyFlowUi). Keeps display_driver.cpp free
// of tileview/screen-content knowledge, same separation it already had with
// CockpitUi before this existed. All lv_* calls happen from the LVGL task
// (see display_driver.cpp) -- build()/update() must only ever be called
// from there.
namespace AppUi {

// Constructs the tileview and both tiles. Call after lv_disp_drv_register().
void build();

// Pushes a fresh VehicleState/clock into both tiles. LVGL only redraws the
// currently visible tile, so updating both unconditionally is cheap and
// keeps this simple -- same tradeoff CockpitUi::update() already made.
void update(const VehicleState &state, time_t clockEpoch);

} // namespace AppUi
