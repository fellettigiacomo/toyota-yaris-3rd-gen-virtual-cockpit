#include "touch_nav.h"
#include "board_pins.h"

#include <Arduino.h>
#include <Wire.h>

// Talks to the AXS15231B touch controller directly over I2C via the plain
// Arduino Wire API, replicating the read Waveshare's own working LVGL_V8
// example uses (09_LVGL_V8_Test/lvgl_port.c, example_lvgl_touch_cb) --
// WITHOUT pulling in the esp_lcd_touch component. This project only needs
// "is a finger down, yes/no" (the point-count byte), not coordinates or
// gestures, so a short Wire read is enough, and it sidesteps depending on
// the exact esp_lcd_panel_io_i2c shape of whatever ESP-IDF minor version
// this Arduino core pins (that layer's API changed across IDF releases;
// Wire's has not). See src/WAVESHARE_VENDORED.md.
//
// Three details MUST match that reference exactly or the read misbehaves
// (all learned the hard way -- a mismatched version phantom-cycled the
// screen once at boot):
//   1. the write and the read are ONE transaction with a repeated START
//      (Wire.endTransmission(false), no STOP in between) -- the controller
//      returns stale/garbage data if a STOP splits them, exactly what
//      i2c_master_write_read_dev / i2c_master_transmit_receive does;
//   2. the command's length field (byte[7] = 0x0e) and the 32-byte read
//      length are taken verbatim from the example;
//   3. a touch counts as present only when the point-count byte is 1..4
//      (buff[1] > 0 && buff[1] < 5). A bare ">= 1" wrongly treats the 0xFF
//      an idle/floating bus returns as a permanent touch.
//
// No reset/interrupt GPIO is toggled here: user_config.h's
// EXAMPLE_PIN_NUM_TOUCH_RST/_INT are both -1 on this board -- the touch
// die shares the panel's physical reset line (PIN_LCD_RESET), already
// toggled by display_driver.cpp's initPanel() before this module's begin()
// runs.
namespace TouchNav {

namespace {

constexpr uint8_t kTouchI2cAddr = 0x3B; // ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS

// AXS15231B "read touch data" command frame, verbatim from the Waveshare
// example: byte[7] = 0x0e is the expected response length field, and the
// master then clocks out kReadLen bytes.
constexpr uint8_t kReadCmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00,
                                   0x00, 0x0E, 0x00, 0x00, 0x00};
constexpr int kReadLen = 32;

constexpr uint32_t kDebounceMs = 30; // matches ScreenNav's BOOT-button debounce

bool g_rawDown = false;
bool g_stableDown = false;
uint32_t g_lastChangeMs = 0;

bool readTouchedRaw() {
    Wire.beginTransmission(kTouchI2cAddr);
    Wire.write(kReadCmd, sizeof(kReadCmd));
    // endTransmission(false): repeated START, keep the bus -- the read below
    // is the same transaction, matching i2c_master_write_read_dev. A STOP
    // here (the default true) makes the controller return garbage.
    if (Wire.endTransmission(false) != 0) {
        return false; // NACK/bus error -- treat as "not touched", never block/retry here
    }
    if (Wire.requestFrom(static_cast<int>(kTouchI2cAddr), kReadLen) != kReadLen) {
        return false;
    }
    uint8_t data[kReadLen];
    for (int i = 0; i < kReadLen; i++) {
        data[i] = Wire.available() ? static_cast<uint8_t>(Wire.read()) : 0;
    }
    // buff[1] is the touch point count. Present only when 1..4 -- the same
    // test the Waveshare example uses; a bare ">= 1" would latch on the 0xFF
    // an idle/floating bus returns and phantom-cycle the screen.
    uint8_t numPoints = data[1];
    return numPoints > 0 && numPoints < 5;
}

} // namespace

void begin() {
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    Wire.setClock(300000); // matches the Waveshare example's touch bus speed
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
