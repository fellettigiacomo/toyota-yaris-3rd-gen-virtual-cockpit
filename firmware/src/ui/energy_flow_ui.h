#pragma once

#include <lvgl.h>
#include "vehicle_state.h"

// Third screen: where the energy is going. Engine consumption on the left,
// HV pack current on the right, and particles funnelling from each source
// into the cockpit's speed (the wheels), or out of it towards the pack when
// it charges. All lv_* calls happen from the LVGL task, same rule as
// CockpitUi.
namespace EnergyFlowUi {

// Builds the readouts and the two funnels onto `parent` (a plain full-screen
// container -- see AppUi::build()).
void build(lv_obj_t *parent);

// Derives flow directions from the latest VehicleState, refreshes the
// readouts and advances the particles. Call at the UI sync cadence.
void update(const VehicleState &state);

} // namespace EnergyFlowUi
