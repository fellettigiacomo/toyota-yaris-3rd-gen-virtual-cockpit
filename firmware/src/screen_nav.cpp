#include "screen_nav.h"
#include "board_pins.h"

#include <Arduino.h>

namespace ScreenNav {

namespace {

constexpr uint32_t kDebounceMs = 30;

bool g_stableLow = false; // debounced state: true while the button is held
bool g_rawLow = false;    // last raw reading
uint32_t g_lastChangeMs = 0;

} // namespace

void begin() {
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
}

bool pressed() {
    bool rawLow = (digitalRead(PIN_BOOT_BUTTON) == LOW);
    uint32_t nowMs = millis();

    if (rawLow != g_rawLow) {
        g_rawLow = rawLow;
        g_lastChangeMs = nowMs;
    }

    bool firedThisCall = false;
    if (g_rawLow != g_stableLow && (nowMs - g_lastChangeMs) >= kDebounceMs) {
        g_stableLow = g_rawLow;
        firedThisCall = g_stableLow; // fire only on the press edge, not release
    }
    return firedThisCall;
}

} // namespace ScreenNav
