#include "touch_input.h"
#include "user_config.h"
#include "axs15231b/esp_lcd_axs15231b.h"

#include <Arduino.h>
#include <lvgl.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"

// UNVERIFIED ON REAL HARDWARE -- see the project-level gotcha list this was
// written against: the touch chip glue (this file) was NOT part of what
// WAVESHARE_VENDORED.md says was ported -- only the low-level chip driver
// (axs15231b/esp_lcd_axs15231b.c's esp_lcd_touch_new_i2c_axs15231b(), and
// the generic touch/esp_lcd_touch.c wrapper) is. This file supplies the
// missing glue: standing up the I2C bus as an esp_lcd_panel_io_handle_t
// (the touch driver wants that, not the raw i2c_master_dev_handle_t that
// i2c_bsp.c already builds) and registering an LVGL pointer indev.
//
// Deliberately does NOT call i2c_bsp.c's i2c_master_Init() -- that stands
// up BOTH i2c ports in one call, but port 0 (RTC/IMU, GPIO47/48) is already
// live via the Arduino Wire library inside rtc_clock.cpp. Calling it here
// would create a second, competing master driver on the same physical pins
// through the new-style driver/i2c_master.h API and risk breaking the RTC.
// This only brings up port 1 (touch-only, GPIO18/17), standalone.
//
// The swap_xy/mirror_x/mirror_y below are DERIVED, not measured: worked out
// algebraically from display_driver.cpp's rotateToNative() index math
// (native[j*172+i] = landscape[(171-i)*640+j], i.e. landscape_x=native_y,
// landscape_y=171-native_x) composed with the exact order of operations in
// touch/esp_lcd_touch.c's esp_lcd_touch_get_coordinates() (mirror_x, then
// mirror_y, then swap_xy, with x_max/y_max applied to the PRE-swap x/y
// slots) -- see the working below. This assumes the touch digitizer's raw
// coordinate origin/orientation exactly matches the LCD's native pixel
// grid, which is the common case for a bonded touch-on-panel like this one
// but is exactly the kind of assumption that needs a real-hardware check:
// touch known corners and see if the reported LVGL point lands where
// expected, adjust the three flags/x_max/y_max if not.
//
//   raw touch coords: raw_x in [0, kNativeTouchXMax], raw_y in [0, kNativeTouchYMax]
//   step 1 (mirror_x):  x' = kNativeTouchXMax - raw_x   (mirror_y off, y'=raw_y)
//   step 2 (swap_xy):   final_x = y' = raw_y
//                       final_y = x' = kNativeTouchXMax - raw_x
//   desired (from rotateToNative): lv_x = raw_y, lv_y = 171 - raw_x
//   => matches exactly when kNativeTouchXMax = 171.

namespace TouchInput {

namespace {

constexpr uint16_t kNativeTouchXMax = 171; // native panel width (172) - 1
constexpr uint16_t kNativeTouchYMax = 639; // native panel height (640) - 1, unused (mirror_y off)

esp_lcd_touch_handle_t g_touchHandle = nullptr;

// Diagnostic-only state -- not needed once touch is confirmed working, but
// this is the only way to see what the chip is actually reporting without
// a debugger attached.
uint32_t g_lastLogMs = 0;
esp_err_t g_lastReadErr = ESP_OK;

void touchReadCb(lv_indev_drv_t *, lv_indev_data_t *data) {
    esp_err_t err = esp_lcd_touch_read_data(g_touchHandle);
    uint32_t nowMs = millis();

    if (err != ESP_OK) {
        if (err != g_lastReadErr || nowMs - g_lastLogMs > 2000) {
            Serial.printf("[touch_input] esp_lcd_touch_read_data failed: %s\n", esp_err_to_name(err));
            g_lastLogMs = nowMs;
        }
        g_lastReadErr = err;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    g_lastReadErr = ESP_OK;

    uint16_t x = 0, y = 0;
    uint8_t pointNum = 0;
    bool pressed = esp_lcd_touch_get_coordinates(g_touchHandle, &x, &y, nullptr, &pointNum, 1);

    if (pressed && pointNum > 0) {
        if (nowMs - g_lastLogMs > 200) { // throttled so it's readable while dragging a finger
            Serial.printf("[touch_input] touched: lvgl point x=%u y=%u\n", x, y);
            g_lastLogMs = nowMs;
        }
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

} // namespace

void begin() {
    i2c_master_bus_config_t busConfig = {};
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.i2c_port = I2C_NUM_1;
    busConfig.scl_io_num = Touch_SCL_NUM;
    busConfig.sda_io_num = Touch_SDA_NUM;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t busHandle = nullptr;
    ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &busHandle));

    // NOTE: ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG_EX(scl_speed_hz) (the vendored
    // header's own "pass an explicit clock" variant) is unusable as a macro
    // call: its parameter is named scl_speed_hz, the exact same token as the
    // ".scl_speed_hz" designated-initializer field it expands into, so the
    // preprocessor substitutes both occurrences and emits ".300000 = 300000,"
    // -- a syntax error, confirmed by an actual build. Use the plain macro
    // (which leaves scl_speed_hz at its designated-init default of 0) and set
    // the clock as a normal follow-up assignment instead, same 300kHz i2c_bsp.c
    // already uses for this same touch device.
    esp_lcd_panel_io_i2c_config_t ioConfig = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    ioConfig.scl_speed_hz = 300000;
    esp_lcd_panel_io_handle_t panelIo = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(busHandle, &ioConfig, &panelIo));

    esp_lcd_touch_config_t touchConfig = {};
    touchConfig.x_max = kNativeTouchXMax;
    touchConfig.y_max = kNativeTouchYMax;
    touchConfig.rst_gpio_num = GPIO_NUM_NC; // EXAMPLE_PIN_NUM_TOUCH_RST == -1, no reset line wired
    touchConfig.int_gpio_num = GPIO_NUM_NC; // EXAMPLE_PIN_NUM_TOUCH_INT == -1, no interrupt line -- poll instead
    touchConfig.flags.swap_xy = 1;
    touchConfig.flags.mirror_x = 1;
    touchConfig.flags.mirror_y = 0;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs15231b(panelIo, &touchConfig, &g_touchHandle));

    static lv_indev_drv_t s_indevDrv;
    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = touchReadCb;
    lv_indev_drv_register(&s_indevDrv);

    Serial.println("[touch_input] AXS15231B touch indev registered");
}

} // namespace TouchInput
