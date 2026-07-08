#include "display_driver.h"
#include "board_pins.h"
#include "app_config.h"
#include "can_decoder.h"
#include "rtc_clock.h"
#include "ui/cockpit_ui.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {

Arduino_DataBus *g_bus = nullptr;
Arduino_GFX *g_gfx = nullptr;

lv_disp_draw_buf_t g_drawBuf;
lv_disp_drv_t g_dispDrv;
lv_color_t *g_buf1 = nullptr;
lv_color_t *g_buf2 = nullptr;

esp_timer_handle_t g_tickTimer = nullptr;

void initGfx() {
    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_LCD_BACKLIGHT, HIGH);

    g_bus = new Arduino_ESP32QSPI(PIN_LCD_QSPI_CS, PIN_LCD_QSPI_SCK,
                                   PIN_LCD_QSPI_D0, PIN_LCD_QSPI_D1,
                                   PIN_LCD_QSPI_D2, PIN_LCD_QSPI_D3);
    g_gfx = new Arduino_AXS15231B(g_bus, PIN_LCD_RESET, LCD_ROTATION, true,
                                   LCD_PANEL_NATIVE_WIDTH, LCD_PANEL_NATIVE_HEIGHT);
    g_gfx->begin();
    g_gfx->fillScreen(0x0000); // RGB565 black -- matches the design's #0a0b0d closely enough pre-LVGL-clear
}

// LVGL flush callback: pushes one invalidated rectangle from an LVGL draw
// buffer straight to the panel via Arduino_GFX's QSPI bitmap blit.
//
// LV_COLOR_16_SWAP is left at 0 in lv_conf.h as a starting guess -- flip it
// if colors come out channel/byte-swapped on first boot (see the plan doc's
// "Known unknowns"); this is the one part of the bridge that could not be
// verified without real hardware in this session.
void dispFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    g_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(color_p), w, h);
    lv_disp_flush_ready(disp);
}

void IRAM_ATTR onLvglTick(void *) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void initLvgl() {
    lv_init();

    size_t pixels = static_cast<size_t>(g_gfx->width()) * static_cast<size_t>(g_gfx->height());
    size_t bufBytes = pixels * sizeof(lv_color_t);

    // Two full-screen buffers in PSRAM: at 640x172x16bpp (~215KB each) this
    // is trivial against the board's 8MB PSRAM, and avoids partial-buffer
    // bookkeeping while the design is still being visually tuned -- see the
    // plan doc's Display driver section for the reasoning.
    g_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_buf1 == nullptr || g_buf2 == nullptr) {
        Serial.println("[display_driver] FATAL: PSRAM draw buffer allocation failed");
        while (true) {
            delay(1000);
        }
    }

    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, pixels);

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = g_gfx->width();
    g_dispDrv.ver_res = g_gfx->height();
    g_dispDrv.flush_cb = dispFlush;
    g_dispDrv.draw_buf = &g_drawBuf;
    g_dispDrv.full_refresh = 0;
    lv_disp_drv_register(&g_dispDrv);

    const esp_timer_create_args_t tickArgs = {
        .callback = &onLvglTick,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lv_tick",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&tickArgs, &g_tickTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(g_tickTimer, LVGL_TICK_PERIOD_MS * 1000));
}

void lvglTask(void *) {
    initGfx();
    initLvgl();
    CockpitUi::build();

    uint32_t lastSyncMs = 0;
    for (;;) {
        lv_timer_handler();

        uint32_t nowMs = millis();
        if (nowMs - lastSyncMs >= UI_SYNC_INTERVAL_MS) {
            lastSyncMs = nowMs;
            VehicleState state = CanDecoder::getSnapshot();
            time_t clockEpoch = RtcClock::isValid() ? RtcClock::now() : 0;
            CockpitUi::update(state, clockEpoch);
        }

        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_DELAY_MS));
    }
}

} // namespace

namespace DisplayDriver {

void begin() {
    xTaskCreatePinnedToCore(lvglTask, "lvgl", STACK_SIZE_LVGL, nullptr,
                             TASK_PRIO_LVGL, nullptr, CORE_LVGL);
}

} // namespace DisplayDriver
