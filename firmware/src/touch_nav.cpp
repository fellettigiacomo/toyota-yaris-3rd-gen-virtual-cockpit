#include "touch_nav.h"
#include "board_pins.h"

#include <Arduino.h>
#include <Wire.h>

// Talks to the AXS15231B touch controller directly over I2C via the plain
// Arduino Wire API, replicating the read protocol byte-for-byte from the
// vendored esp_lcd_touch_new_i2c_axs15231b() driver
// (src/axs15231b/esp_lcd_axs15231b.c, touch_axs15231b_read_data()) --
// WITHOUT calling that driver. This project only needs "is a finger down,
// yes/no" (the point-count byte), not coordinates or gestures, so a ~15-line
// Wire read is enough, and it sidesteps depending on the exact
// esp_lcd_panel_io_i2c shape of whatever ESP-IDF minor version this
// Arduino core pins (that layer's API changed across IDF releases; Wire's
// has not). See src/WAVESHARE_VENDORED.md.
//
// No reset/interrupt GPIO is toggled here: user_config.h's
// EXAMPLE_PIN_NUM_TOUCH_RST/_INT are both -1 on this board -- the touch
// die shares the panel's physical reset line (PIN_LCD_RESET), already
// toggled by display_driver.cpp's initPanel() before this module's begin()
// runs.
namespace TouchNav {

namespace {

constexpr uint8_t kTouchI2cAddr = 0x3B; // ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS

// AXS15231B "read touch data" command frame, fixed per the vendored driver:
// bytes [6:7] are the big-endian expected response length, AXS_MAX_TOUCH_
// NUMBER(1) * 6 + 2 = 8 there, so hardcoded here rather than computed.
constexpr uint8_t kReadCmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00,
                                   0x00, 0x08, 0x00, 0x00, 0x00};
constexpr uint8_t kReadLen = 8;

constexpr uint32_t kDebounceMs = 30; // matches ScreenNav's BOOT-button debounce

bool g_rawDown = false;
bool g_stableDown = false;
uint32_t g_lastChangeMs = 0;

bool readTouchedRaw() {
    Wire.beginTransmission(kTouchI2cAddr);
    Wire.write(kReadCmd, sizeof(kReadCmd));
    if (Wire.endTransmission() != 0) {
        return false; // NACK/bus error -- treat as "not touched", never block/retry here
    }
    if (Wire.requestFrom(static_cast<int>(kTouchI2cAddr), static_cast<int>(kReadLen)) != kReadLen) {
        return false;
    }
    uint8_t data[kReadLen];
    for (uint8_t i = 0; i < kReadLen; i++) {
        data[i] = Wire.available() ? static_cast<uint8_t>(Wire.read()) : 0;
    }
    uint8_t numPoints = data[1]; // AXS_TOUCH_POINT_NUM_POS
    return numPoints >= 1;
}

} // namespace

void begin() {
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    Wire.setClock(400000);
}

bool pressed() {
    bool rawDown = readTouchedRaw();
    uint32_t nowMs = millis();

    if (rawDown != g_rawDown) {
        g_rawDown = rawDown;
        g_lastChangeMs = nowMs;
    }

    bool firedThisCall = false;
    if (g_rawDown != g_stableDown && (nowMs - g_lastChangeMs) >= kDebounceMs) {
        g_stableDown = g_rawDown;
        firedThisCall = g_stableDown; // fire only on the press edge, not release
    }
    return firedThisCall;
}

} // namespace TouchNav
