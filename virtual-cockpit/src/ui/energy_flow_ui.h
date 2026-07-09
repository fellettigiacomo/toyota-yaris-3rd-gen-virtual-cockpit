#pragma once

#include <lvgl.h>
#include "vehicle_state.h"

// The second screen (reached by pressing the BOOT button, see screen_nav.h):
// a qualitative ENGINE/MOTOR/BATTERY/WHEELS energy-flow diagram,
// direction-only (no kW -- HV pack current/voltage aren't on this bus, see
// docs/signal_findings.md). All lv_* calls happen from the LVGL task, same
// rule as CockpitUi.
namespace EnergyFlowUi {

// Builds the node icons + flow arrows onto `parent` (a plain full-screen
// container -- see AppUi::build()).
void build(lv_obj_t *parent);

// Derives flow directions from the latest VehicleState and advances the
// arrow animations. Call at the same cadence CockpitUi::update() runs at.
void update(const VehicleState &state);

} // namespace EnergyFlowUi
