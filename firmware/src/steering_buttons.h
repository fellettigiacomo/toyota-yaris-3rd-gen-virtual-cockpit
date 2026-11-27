#pragma once

// Steering-wheel buttons on the radio's SW2 line, read by voltage.
//
// The pink SW2 wire is pulled out of the radio's 28-pin connector (pin 22)
// and wired here instead; SWG (pin 23, red) stays on the radio and is
// tapped for ground:
//
//   3V3 --[10k]--+-- SW2 (pink, steering-wheel side)
//                |
//                +--[1k]-- PIN_STEERING_SW2
//   GND ----------------- SWG (red)
//
// MODE shorts SW2 to SWG (~0 V). On Hook / Off Hook go through resistors
// (both measured below ~1 V on this car), and a fixed resistor across the
// pad keeps the idle level high. The 10k must be external: the internal
// pull-up (~45k) alone would pull the phone buttons down near MODE's level.
// begin() still enables it, so a board without this mod idles instead of
// reading a floating pin as random presses.
namespace SteeringButtons {

enum class Button { None, Mode, Other };

// Claims the pin for the ADC, re-enables its internal pull-up (ADC setup
// turns it off) and starts the sampling task. Call once at startup.
void begin();

// Returns the oldest debounced press not yet taken, None if there is none.
// Safe to call from any task, at any rate: presses queue up in between.
Button pressed();

} // namespace SteeringButtons
