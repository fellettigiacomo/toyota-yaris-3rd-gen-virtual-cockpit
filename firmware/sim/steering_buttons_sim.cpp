// Desktop stand-in for src/steering_buttons.cpp (no ADC, no FreeRTOS here):
// the steering wheel is never pressed; the spacebar already stands in for
// the BOOT button.
#include "steering_buttons.h"

namespace SteeringButtons {
void begin() {}
Button pressed() { return Button::None; }
} // namespace SteeringButtons
