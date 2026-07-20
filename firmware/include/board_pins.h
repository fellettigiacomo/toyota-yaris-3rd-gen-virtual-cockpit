#pragma once

#include <cstdint>

// Pin mapping for the Waveshare ESP32-S3-Touch-LCD-3.49, board silkscreen "Rev1.1".
// Onboard peripheral pins (display/RTC/SD) were extracted from Waveshare's own
// official example code (github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49,
// Examples/Arduino/09_LVGL_V8_Test/user_config.h, 02_I2C_PCF85063/rtc_bsp.cpp,
// 04_SD_Card/sdcard_bsp.cpp) rather than guessed.
//
// The CAN transceiver pins were chosen from the 22-pin expansion header GPIOs
// actually confirmed present on the user's board (photographed silkscreen):
// 0, 1, 2, 3, 4, 5, 19, 20, 38, 39, 40, 41, 43, 44 (+ 3V3/BAT/G/5V/SDA/SCL).
// Excluded from consideration: GPIO0/3 (boot strapping, GPIO0 already wired to
// the BOOT button), GPIO19/20 (native USB D-/D+ -- shares the USB-C port used
// for flashing/serial monitor, do NOT repurpose), GPIO39/40/41 (already used
// internally by the onboard SDMMC card slot), GPIO43/44 (UART0, used by the
// USB-serial bridge for flashing/serial monitor), SDA/SCL (GPIO47/48, already
// used internally for the RTC/IMU I2C bus -- also electrically incompatible
// with the CAN transceiver's digital TX/RX signaling anyway).

// --- AXS15231B display, QSPI ---
// virtual-cockpit drives this via ESP-IDF's esp_lcd component directly
// (SPI3_HOST, see src/display_driver.cpp), not Arduino_GFX/SPI2_HOST as
// obd-capture does -- LCD_ROTATION below is unused here, this project always
// rotates the frame in software instead (see display_driver.cpp for why).
constexpr int PIN_LCD_QSPI_CS   = 9;
constexpr int PIN_LCD_QSPI_SCK  = 10;
constexpr int PIN_LCD_QSPI_D0   = 11;
constexpr int PIN_LCD_QSPI_D1   = 12;
constexpr int PIN_LCD_QSPI_D2   = 13;
constexpr int PIN_LCD_QSPI_D3   = 14;
constexpr int PIN_LCD_RESET     = 21;
constexpr int PIN_LCD_BACKLIGHT = 8;

constexpr int LCD_PANEL_NATIVE_WIDTH  = 172; // physical panel width (portrait)
constexpr int LCD_PANEL_NATIVE_HEIGHT = 640; // physical panel height (portrait)
constexpr int LCD_ROTATION = 1;              // rotate to landscape 640x172 for the UI

// --- Shared sensor I2C bus (PCF85063 RTC + QMI8658 IMU) ---
// Not used by this project (no RTC/IMU reads) -- kept documented in case a
// future revision needs it. This is a *different* I2C bus from the
// capacitive touch controller below.
constexpr int PIN_SENSOR_I2C_SDA = 47;
constexpr int PIN_SENSOR_I2C_SCL = 48;
constexpr uint8_t I2C_ADDR_RTC_PCF85063 = 0x51;

// --- Capacitive touch (AXS15231B, same die as the display panel) ---
// Read directly over I2C via touch_nav.cpp (plain Arduino Wire, not the
// vendored esp_lcd_touch component -- see that file's header comment for
// why). Screens cycle on any tap, same action as the BOOT button.
constexpr int PIN_TOUCH_SDA = 17;
constexpr int PIN_TOUCH_SCL = 18;

// --- microSD card, native SDMMC 1-bit mode (NOT SPI) ---
constexpr int PIN_SD_CMD = 39;
constexpr int PIN_SD_CLK = 41;
constexpr int PIN_SD_D0  = 40;

// --- 22-pin expansion header: CAN transceiver (SN65HVD230) ---
// RS pin on the SN65HVD230 breakout should be wired directly to GND (always
// high-speed mode). Leave the breakout's onboard 120-ohm termination jumper
// OPEN/disabled -- the vehicle's OBD-II bus is already terminated at both ends.
constexpr int PIN_TWAI_TX = 1; // ESP32 GPIO1 -> SN65HVD230 "TX" (D input)
constexpr int PIN_TWAI_RX = 2; // ESP32 GPIO2 <- SN65HVD230 "RX" (R output)

// --- Physical BOOT button, used here to cycle screens (see screen_nav.h) ---
constexpr int PIN_BOOT_BUTTON = 0; // active LOW
