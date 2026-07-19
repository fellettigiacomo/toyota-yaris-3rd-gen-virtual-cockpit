#pragma once

// Debounced reader for the panel's built-in capacitive touch controller
// (AXS15231B, I2C, see board_pins.h), used as a "tap anywhere cycles the
// screen" input alongside the physical BOOT button (ScreenNav). Only
// presence/absence of a touch is used -- no coordinates, no gestures -- so
// any tap on any of the three screens advances to the next one.
namespace TouchNav {

// Starts the dedicated touch I2C bus. Call once at startup, after the LCD
// panel has been reset/initialized (the touch controller shares the
// panel's physical reset line -- see touch_nav.cpp).
void begin();

// Returns true exactly once per debounced touch-down edge -- call every
// loop tick, same contract as ScreenNav::pressed().
bool pressed();

} // namespace TouchNav
