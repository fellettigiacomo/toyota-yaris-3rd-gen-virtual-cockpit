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
// pad keeps the idle level high. The pull-up must be external: analogRead
// puts the pin in analog mode, which disconnects the internal pull-up.
namespace SteeringButtons {

enum class Button { None, Mode, Other };

// Returns a button exactly once per debounced press, None otherwise --
// call every loop tick.
Button pressed();

} // namespace SteeringButtons
