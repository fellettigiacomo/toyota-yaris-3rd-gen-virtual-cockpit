#include "touch_input.h"
#include "user_config.h"

#include <Arduino.h>
#include <lvgl.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// Rewritten from Waveshare's own proven reference example
// (Arduino/09_LVGL_V8_Test/lvgl_port.c's example_lvgl_touch_cb() +
// i2c_bsp.c/.h, user-supplied) after the esp_lcd_touch/esp_lcd_panel_io_i2c
// path (the previous version of this file) produced zero touch response on
// real hardware. Their reference does not use the esp_lcd_touch abstraction
// at all: a raw combined I2C write+read transaction on a plain
// i2c_master_dev_handle_t, using the exact command bytes and
// coordinate-extraction bit-masking below (copied verbatim from their code).
//
// Coordinate mapping: their reference runs LVGL natively portrait
// (172w x 640h, their "Rotated == USER_DISP_ROT_NONO" case) and computes
// data->point.x = rawY, data->point.y = (640 - rawX) -- i.e. the touch
// chip's raw "X" axis is the panel's LONG (640) physical dimension and raw
// "Y" is the SHORT (172) dimension. This project runs LVGL landscape
// (640w x 172h, software-transposed by display_driver.cpp's
// rotateToNative()). Composing their axis identification with our own
// transpose math gives a pure double-mirror with NO axis swap:
// lvgl_x = 640 - rawX, lvgl_y = 172 - rawY -- matching the structural form
// of their own (unexercised in their demo, Rotated defaults to NONO)
// "Rotated == USER_DISP_ROT_90" branch, `point.x = H_RES-pointX,
// point.y = V_RES-pointY`, with H_RES/V_RES substituted for our actual
// 640x172 landscape.
//
// Still does NOT call i2c_bsp.c's i2c_master_Init() -- see the reasoning
// below in begin(); this brings up the touch-only I2C port standalone.

namespace TouchInput {

namespace {

constexpr int kLogicalW = 640; // this project's LVGL landscape width
constexpr int kLogicalH = 172; // this project's LVGL landscape height

i2c_master_dev_handle_t g_touchDev = nullptr;
uint32_t g_lastLogMs = 0;

void touchReadCb(lv_indev_drv_t *, lv_indev_data_t *data) {
    // Exact command Waveshare's reference sends (lvgl_port.c's
    // example_lvgl_touch_cb): {0xb5,0xab,0xa5,0x5a,...,0x0e,...}, read 32
    // bytes back in the same transaction.
    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};
    uint8_t buff[32] = {0};

    esp_err_t err = i2c_master_transmit_receive(g_touchDev, cmd, sizeof(cmd), buff, sizeof(buff), 50);
    uint32_t nowMs = millis();

    if (err != ESP_OK) {
        if (nowMs - g_lastLogMs > 2000) {
            Serial.printf("[touch_input] i2c_master_transmit_receive failed: %s\n", esp_err_to_name(err));
            g_lastLogMs = nowMs;
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // buff[1] is the touch-point count (1..4 == touched); buff[2]/[3] pack
    // rawX as a 12-bit big-endian value split across a 4-bit-high nibble +
    // low byte, buff[4]/[5] pack rawY the same way -- identical bit layout
    // to Waveshare's reference.
    uint16_t rawX = ((static_cast<uint16_t>(buff[2]) & 0x0f) << 8) | buff[3];
    uint16_t rawY = ((static_cast<uint16_t>(buff[4]) & 0x0f) << 8) | buff[5];

    if (buff[1] > 0 && buff[1] < 5) {
        if (rawX > kLogicalW - 1) rawX = kLogicalW - 1;
        if (rawY > kLogicalH - 1) rawY = kLogicalH - 1;
        int lvX = kLogicalW - static_cast<int>(rawX);
        int lvY = kLogicalH - static_cast<int>(rawY);
        if (lvX >= kLogicalW) lvX = kLogicalW - 1;
        if (lvY >= kLogicalH) lvY = kLogicalH - 1;

        if (nowMs - g_lastLogMs > 200) { // throttled so it's readable while dragging
            Serial.printf("[touch_input] touched: raw x=%u y=%u -> lvgl x=%d y=%d\n", rawX, rawY, lvX, lvY);
            g_lastLogMs = nowMs;
        }
        data->point.x = lvX;
        data->point.y = lvY;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

} // namespace

void begin() {
    // Deliberately does NOT call i2c_bsp.c's i2c_master_Init() -- that stands
    // up BOTH i2c ports in one call, but port 0 (RTC/IMU, GPIO47/48) is
    // already live via the Arduino Wire library inside rtc_clock.cpp.
    // Calling it here would create a second, competing master driver on the
    // same physical pins through the new-style driver/i2c_master.h API and
    // risk breaking the RTC. This only brings up port 1 (touch-only,
    // GPIO18/17), standalone.
    i2c_master_bus_config_t busConfig = {};
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.i2c_port = I2C_NUM_1;
    busConfig.scl_io_num = Touch_SCL_NUM;
    busConfig.sda_io_num = Touch_SDA_NUM;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t busHandle = nullptr;
    ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &busHandle));

    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = I2C_TOUCH_ADDR;
    devConfig.scl_speed_hz = 300000; // same clock i2c_bsp.c uses for this same device
    ESP_ERROR_CHECK(i2c_master_bus_add_device(busHandle, &devConfig, &g_touchDev));

    static lv_indev_drv_t s_indevDrv;
    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = touchReadCb;
    lv_indev_drv_register(&s_indevDrv);

    Serial.println("[touch_input] AXS15231B touch indev registered (raw I2C, Waveshare reference command)");
}

} // namespace TouchInput
