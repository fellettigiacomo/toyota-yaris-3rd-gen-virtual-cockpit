#include "display_driver.h"
#include "board_pins.h"
#include "app_config.h"
#include "can_decoder.h"
#include "ui/app_ui.h"
#include "screen_nav.h"
#include "touch_nav.h"
#include "axs15231b/esp_lcd_axs15231b.h"

#include <Arduino.h>
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

// Drives the AXS15231B over QSPI via ESP-IDF's esp_lcd component + Waveshare's
// own panel driver (ported verbatim into src/axs15231b/, src/touch/,
// src/lcd_bl_bsp/ from their official 09_LVGL_V8_Test/10_LVGL_V9_Test
// examples), NOT Arduino_GFX.
//
// Why: this panel, in QSPI mode, has no row-seek command -- panel_axs15231b
// _draw_bitmap() deliberately never sends RASET when use_qspi_interface=1.
// Its RAMWR/RAMWRC protocol only supports writing the *entire* frame,
// sequentially, top-to-bottom, every time (RAMWR resets the internal write
// pointer to the top, RAMWRC continues from wherever it currently is).
// Arduino_GFX's generic AXS15231B driver doesn't know this and happily
// issues arbitrary partial-rectangle writes -- exactly what LVGL's default
// partial-refresh flush does -- which desyncs the panel's internal write
// pointer. That is what produced the flicker/split-screen/black-line
// corruption on real hardware (both here and in obd-capture, which used the
// same Arduino_GFX approach).
//
// The fix, matching Waveshare's own official example byte-for-byte:
// `full_refresh=1` (LVGL always hands the flush callback the complete
// frame) and drive the panel in its native 172(w)x640(h) portrait
// orientation, rotating the full landscape frame into a native-orientation
// scratch buffer in software before streaming it out in fixed-size DMA
// chunks -- there is no working hardware-rotation path for this panel on
// this board revision (Waveshare's own comment for this: "软件实现旋转",
// "software-implemented rotation").
namespace {

// --- Native (physical) vs logical (LVGL/UI) orientation ---
constexpr int kNativeW = LCD_PANEL_NATIVE_WIDTH;  // 172, physical panel width
constexpr int kNativeH = LCD_PANEL_NATIVE_HEIGHT; // 640, physical panel height
constexpr int kLogicalW = kNativeH; // 640, LVGL/UI landscape width
constexpr int kLogicalH = kNativeW; // 172, LVGL/UI landscape height

constexpr int kChunkRows = 64; // native rows per DMA chunk, matches Waveshare's example
static_assert(kNativeH % kChunkRows == 0, "kNativeH must divide evenly into kChunkRows-sized chunks");
constexpr int kChunkCount = kNativeH / kChunkRows;
constexpr size_t kDmaChunkBytes = static_cast<size_t>(kNativeW) * kChunkRows * sizeof(uint16_t);
constexpr size_t kFullFrameBytes = static_cast<size_t>(kLogicalW) * kLogicalH * sizeof(uint16_t);

esp_lcd_panel_handle_t g_panel = nullptr;
SemaphoreHandle_t g_flushSem = nullptr;

uint16_t *g_dmaChunkBuf = nullptr;    // small, MALLOC_CAP_DMA, internal RAM
uint16_t *g_nativeFrameBuf = nullptr; // full native-orientation frame, PSRAM scratch

lv_disp_draw_buf_t g_drawBuf;
lv_disp_drv_t g_dispDrv;
lv_color_t *g_buf1 = nullptr;
lv_color_t *g_buf2 = nullptr;

esp_timer_handle_t g_tickTimer = nullptr;

bool IRAM_ATTR onColorTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
    BaseType_t taskWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_flushSem, &taskWoken);
    return taskWoken == pdTRUE;
}

// Rotates the full LVGL landscape frame (kLogicalW x kLogicalH) into the
// panel's native portrait orientation (kNativeW x kNativeH), pixel-for-pixel
// identical to Waveshare's own transpose (see the file header comment).
void rotateToNative(const uint16_t *landscape, uint16_t *native) {
    uint32_t index = 0;
    for (int j = 0; j < kLogicalW; j++) {
        for (int i = 0; i < kLogicalH; i++) {
            native[index++] = landscape[kLogicalW * (kLogicalH - i - 1) + j];
        }
    }
}

void dispFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    // full_refresh=1 guarantees area always covers the whole screen and
    // color_p always holds the complete current frame.
    (void)area;
    rotateToNative(reinterpret_cast<const uint16_t *>(color_p), g_nativeFrameBuf);

    int offsetY1 = 0;
    int offsetY2 = kChunkRows;
    const uint16_t *src = g_nativeFrameBuf;

    xSemaphoreGive(g_flushSem); // prime the first iteration
    for (int i = 0; i < kChunkCount; i++) {
        xSemaphoreTake(g_flushSem, portMAX_DELAY);
        memcpy(g_dmaChunkBuf, src, kDmaChunkBytes);
        esp_lcd_panel_draw_bitmap(g_panel, 0, offsetY1, kNativeW, offsetY2, g_dmaChunkBuf);
        offsetY1 += kChunkRows;
        offsetY2 += kChunkRows;
        src += kNativeW * kChunkRows;
    }
    xSemaphoreTake(g_flushSem, portMAX_DELAY); // wait for the last chunk to finish
    lv_disp_flush_ready(disp);
}

void IRAM_ATTR onLvglTick(void *) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void initPanel() {
    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_LCD_BACKLIGHT, HIGH);

    gpio_config_t rstConf = {};
    rstConf.intr_type = GPIO_INTR_DISABLE;
    rstConf.mode = GPIO_MODE_OUTPUT;
    rstConf.pin_bit_mask = (1ULL << PIN_LCD_RESET);
    rstConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rstConf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&rstConf));

    spi_bus_config_t busConf = {};
    busConf.data0_io_num = PIN_LCD_QSPI_D0;
    busConf.data1_io_num = PIN_LCD_QSPI_D1;
    busConf.sclk_io_num = PIN_LCD_QSPI_SCK;
    busConf.data2_io_num = PIN_LCD_QSPI_D2;
    busConf.data3_io_num = PIN_LCD_QSPI_D3;
    busConf.max_transfer_sz = kDmaChunkBytes;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &busConf, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t panelIo = nullptr;
    esp_lcd_panel_io_spi_config_t ioConf = {};
    ioConf.cs_gpio_num = PIN_LCD_QSPI_CS;
    ioConf.dc_gpio_num = -1;
    ioConf.spi_mode = 3;
    ioConf.pclk_hz = 40 * 1000 * 1000;
    ioConf.trans_queue_depth = 10;
    ioConf.on_color_trans_done = onColorTransDone;
    ioConf.lcd_cmd_bits = 32;
    ioConf.lcd_param_bits = 8;
    ioConf.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &ioConf, &panelIo));

    // Minimal init sequence -- matches Waveshare's own working example
    // (09_LVGL_V8_Test.ino / 10_LVGL_V9_Test.ino), which overrides the
    // driver's elaborate default gamma/timing table with just these two
    // commands. This board/panel apparently doesn't need the rest at boot.
    static const axs15231b_lcd_init_cmd_t kInitCmds[] = {
        {0x11, (uint8_t[]){0x00}, 0, 100}, // SLPOUT
        {0x29, (uint8_t[]){0x00}, 0, 100}, // DISPON
    };
    axs15231b_vendor_config_t vendorConf = {};
    vendorConf.init_cmds = kInitCmds;
    vendorConf.init_cmds_size = sizeof(kInitCmds) / sizeof(kInitCmds[0]);
    vendorConf.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panelConf = {};
    panelConf.reset_gpio_num = -1; // reset is done manually below, not via the panel driver
    panelConf.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panelConf.bits_per_pixel = 16;
    panelConf.vendor_config = &vendorConf;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panelIo, &panelConf, &g_panel));

    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(PIN_LCD_RESET), 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(PIN_LCD_RESET), 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(PIN_LCD_RESET), 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
}

void initLvgl() {
    g_flushSem = xSemaphoreCreateBinary();

    g_dmaChunkBuf = static_cast<uint16_t *>(heap_caps_malloc(kDmaChunkBytes, MALLOC_CAP_DMA));
    g_nativeFrameBuf = static_cast<uint16_t *>(heap_caps_malloc(kFullFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(kFullFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(kFullFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_dmaChunkBuf == nullptr || g_nativeFrameBuf == nullptr || g_buf1 == nullptr || g_buf2 == nullptr) {
        Serial.println("[display_driver] FATAL: display buffer allocation failed");
        while (true) {
            delay(1000);
        }
    }

    lv_init();
    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, static_cast<uint32_t>(kLogicalW) * kLogicalH);

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = kLogicalW;
    g_dispDrv.ver_res = kLogicalH;
    g_dispDrv.flush_cb = dispFlush;
    g_dispDrv.draw_buf = &g_drawBuf;
    g_dispDrv.full_refresh = 1; // mandatory -- see the file header comment
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
    initPanel();
    initLvgl();
    ScreenNav::begin();
    TouchNav::begin(); // after initPanel(): touch die shares the panel's reset line
    AppUi::build();

    uint32_t lastSyncMs = 0;
    for (;;) {
        lv_timer_handler();

        uint32_t nowMs = millis();
        if (nowMs - lastSyncMs >= UI_SYNC_INTERVAL_MS) {
            lastSyncMs = nowMs;
            VehicleState state = CanDecoder::getSnapshot();
            AppUi::update(state);
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
