#include "steering_buttons.h"
#include "board_pins.h"

#include <Arduino.h>

namespace SteeringButtons {

namespace {

constexpr int kSamples = 8;
constexpr uint32_t kModeMaxMv = 300;
// TODO: split Other into On Hook / Off Hook once their levels are logged.
constexpr uint32_t kReleasedMinMv = 1500;
constexpr uint32_t kDebounceMs = 30;

Button g_raw = Button::None;
Button g_stable = Button::None;
uint32_t g_lastChangeMs = 0;

uint32_t readMv() {
    uint32_t sum = 0;
    for (int i = 0; i < kSamples; i++) sum += analogReadMilliVolts(PIN_STEERING_SW2);
    return sum / kSamples;
}

Button classify(uint32_t mv) {
    if (mv < kModeMaxMv) return Button::Mode;
    if (mv < kReleasedMinMv) return Button::Other;
    return Button::None;
}

const char *name(Button button) {
    return button == Button::Mode ? "MODE" : "other";
}

} // namespace

Button pressed() {
    uint32_t mv = readMv();
    Button raw = classify(mv);
    uint32_t nowMs = millis();

    if (raw != g_raw) {
        g_raw = raw;
        g_lastChangeMs = nowMs;
    }
    if (g_raw == g_stable || (nowMs - g_lastChangeMs) < kDebounceMs) return Button::None;

    g_stable = g_raw;
    if (g_stable == Button::None) return Button::None;

    Serial.printf("steering: %s (%u mV)\n", name(g_stable), static_cast<unsigned>(mv));
    return g_stable;
}

} // namespace SteeringButtons
