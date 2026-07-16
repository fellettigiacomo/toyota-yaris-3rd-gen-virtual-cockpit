#pragma once

// Debounced reader for the board's BOOT button (PIN_BOOT_BUTTON, GPIO0),
// repurposed as the "switch screen" input now that touch is not in use.
// Replaces the earlier touch-driven lv_tileview swipe, which was reported
// to make both the transition itself and the energy-flow tile laggy on
// real hardware.
namespace ScreenNav {

// Configures the GPIO. Call once at startup.
void begin();

// Returns true exactly once per debounced press (the LOW-going edge,
// active-low button) -- call every loop tick.
bool pressed();

} // namespace ScreenNav
