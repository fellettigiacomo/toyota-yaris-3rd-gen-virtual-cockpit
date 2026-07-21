#include "display_ui.h"
#include "board_pins.h"
#include "app_config.h"
#include "can_capture.h"
#include "can_id_table.h"
#include "sd_logger.h"
#include "axs15231b/esp_lcd_axs15231b.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cstring>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

// This panel (AXS15231B, QSPI) has no row-seek command in QSPI mode and
// requires the *entire* frame to be written sequentially every refresh --
// see firmware/src/display_driver.cpp's header comment for the full
// hardware explanation (arbitrary partial-rectangle writes, which is what
// Arduino_GFX's generic AXS15231B driver issues, desync the panel's
// internal write pointer and produced flicker/split-screen/black-line
// corruption on real hardware). This module drives the panel with the same
// fix firmware/ uses: Waveshare's own esp_lcd-based panel driver (ported
// verbatim into axs15231b/, touch/ -- see src/WAVESHARE_VENDORED.md),
// native 172(w)x640(h) portrait orientation, full-frame software rotation
// into a landscape 640x172 scratch buffer, and chunked DMA writes.
//
// Unlike firmware/, this project has no LVGL widget tree to source frames
// from -- text/rects are drawn with the Arduino_GFX API this file already
// used, just retargeted from the (buggy, on this panel) real-panel driver
// object to an Arduino_Canvas: a headless, RAM-only framebuffer that never
// touches real hardware (constructed with a null `output`, so its own
// flush()/begin() never dereference it). drawStatsBar()/drawEntryRow()/
// drawIdTable() below are therefore unchanged from the Arduino_GFX version
// of this file -- only the hardware output path (initPanel() +
// flushCanvasToPanel()) is new.
namespace {

Arduino_Canvas *g_canvas = nullptr;
Arduino_GFX *g_gfx = nullptr; // = g_canvas; kept so the drawing code below reads the same either way

constexpr uint16_t COLOR_BG = 0x0000;      // black
constexpr uint16_t COLOR_TEXT = 0xFFFF;    // white
constexpr uint16_t COLOR_HEADER = 0x07FF;  // cyan
constexpr uint16_t COLOR_OK = 0x07E0;      // green
constexpr uint16_t COLOR_WARN = 0xFFE0;    // yellow
constexpr uint16_t COLOR_ERR = 0xF800;     // red
constexpr uint16_t COLOR_DIM = 0x8410;     // gray

constexpr int ROW_H = 10;
constexpr int STATS_BAR_H = 18;
constexpr int COL_W = 320;
constexpr int ENTRIES_PER_COL = (172 - STATS_BAR_H - ROW_H) / ROW_H;
constexpr int ENTRIES_PER_PAGE = ENTRIES_PER_COL * 2;
constexpr uint32_t PAGE_CYCLE_MS = 3000;

uint32_t g_lastPageChangeMs = 0;
size_t g_currentPage = 0;

bool g_bootButtonWasPressed = false;
uint32_t g_bootButtonPressedSinceMs = 0;
constexpr uint32_t BOOT_BUTTON_DEBOUNCE_MS = 200;

// --- Native (physical) vs logical (canvas/UI) orientation, same split as
// firmware/src/display_driver.cpp ---
constexpr int kNativeW = LCD_PANEL_NATIVE_WIDTH;  // 172, physical panel width
constexpr int kNativeH = LCD_PANEL_NATIVE_HEIGHT; // 640, physical panel height
constexpr int kLogicalW = kNativeH; // 640, canvas/UI landscape width
constexpr int kLogicalH = kNativeW; // 172, canvas/UI landscape height

constexpr int kChunkRows = 64; // native rows per DMA chunk, matches firmware/'s driver
static_assert(kNativeH % kChunkRows == 0, "kNativeH must divide evenly into kChunkRows-sized chunks");
constexpr int kChunkCount = kNativeH / kChunkRows;
constexpr size_t kDmaChunkBytes = static_cast<size_t>(kNativeW) * kChunkRows * sizeof(uint16_t);
constexpr size_t kFullFrameBytes = static_cast<size_t>(kLogicalW) * kLogicalH * sizeof(uint16_t);

esp_lcd_panel_handle_t g_panel = nullptr;
SemaphoreHandle_t g_flushSem = nullptr;

uint16_t *g_dmaChunkBuf = nullptr;    // small, MALLOC_CAP_DMA, internal RAM
uint16_t *g_nativeFrameBuf = nullptr; // full native-orientation frame, PSRAM scratch

bool IRAM_ATTR onColorTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
    BaseType_t taskWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_flushSem, &taskWoken);
    return taskWoken == pdTRUE;
}

// Rotates the full canvas landscape frame (kLogicalW x kLogicalH) into the
// panel's native portrait orientation (kNativeW x kNativeH), pixel-for-pixel
// identical to firmware/src/display_driver.cpp's rotateToNative() -- AND
// byte-swaps each pixel to the big-endian wire order this panel expects
// over QSPI (confirmed on real hardware, see firmware/include/lv_conf.h's
// LV_COLOR_16_SWAP comment for the same finding on that board). LVGL does
// this swap itself while rendering into its own buffer; Arduino_Canvas's
// framebuffer holds plain native-endian RGB565, so it's done by hand here.
void rotateAndSwapToNative(const uint16_t *landscape, uint16_t *native) {
    uint32_t index = 0;
    for (int j = 0; j < kLogicalW; j++) {
        for (int i = 0; i < kLogicalH; i++) {
            uint16_t v = landscape[kLogicalW * (kLogicalH - i - 1) + j];
            native[index++] = static_cast<uint16_t>((v >> 8) | (v << 8));
        }
    }
}

// Streams the whole current canvas frame out to the panel in kChunkRows-row
// DMA chunks, same protocol as firmware/'s dispFlush(): the panel's RAMWR/
// RAMWRC only supports writing the entire frame sequentially, so a partial
// redraw is never attempted -- every call here re-sends everything.
void flushCanvasToPanel() {
    rotateAndSwapToNative(g_canvas->getFramebuffer(), g_nativeFrameBuf);

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
}

// Brings up the physical panel via ESP-IDF esp_lcd + the vendored
// axs15231b driver -- ported near-verbatim from
// firmware/src/display_driver.cpp's initPanel()/initLvgl(), minus
// everything LVGL-specific (there's no lv_disp_drv_t here, just the raw
// panel handle and the DMA chunk/scratch buffers flushCanvasToPanel() uses).
void initPanel() {
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

    // Minimal init sequence -- matches Waveshare's own working example and
    // firmware/src/display_driver.cpp's initPanel().
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

    g_flushSem = xSemaphoreCreateBinary();
    g_dmaChunkBuf = static_cast<uint16_t *>(heap_caps_malloc(kDmaChunkBytes, MALLOC_CAP_DMA));
    g_nativeFrameBuf = static_cast<uint16_t *>(heap_caps_malloc(kFullFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_dmaChunkBuf == nullptr || g_nativeFrameBuf == nullptr) {
        Serial.println("[display_ui] FATAL: display buffer allocation failed");
        while (true) {
            delay(1000);
        }
    }
}

void initHardware() {
    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_LCD_BACKLIGHT, HIGH);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    initPanel();

    // Headless framebuffer only (`output` = nullptr, so begin()/flush() never
    // touch real hardware) -- see this file's header comment.
    g_canvas = new Arduino_Canvas(kLogicalW, kLogicalH, nullptr);
    g_canvas->begin();
    g_canvas->fillScreen(COLOR_BG);
    g_gfx = g_canvas;
}

void pollBootButton() {
    bool pressed = (digitalRead(PIN_BOOT_BUTTON) == LOW);
    uint32_t nowMs = millis();
    if (pressed && !g_bootButtonWasPressed) {
        g_bootButtonPressedSinceMs = nowMs;
    }
    if (!pressed && g_bootButtonWasPressed &&
        (nowMs - g_bootButtonPressedSinceMs) >= BOOT_BUTTON_DEBOUNCE_MS) {
        Serial.println("[display_ui] BOOT button pressed -- requesting session stop/rotate");
        SdLogger::requestStop();
    }
    g_bootButtonWasPressed = pressed;
}

const char *hzColorLabel(float hz) {
    return hz > 0 ? "Hz" : "--";
}

void drawStatsBar(const CanCaptureStats &canStats, const SdLoggerStats &sdStats) {
    g_gfx->fillRect(0, 0, g_gfx->width(), STATS_BAR_H, COLOR_BG);
    g_gfx->setTextSize(1);
    g_gfx->setTextColor(COLOR_TEXT);
    g_gfx->setCursor(2, 1);

    uint16_t errColor = (canStats.bus_error_count > 0 || canStats.bus_off_count > 0) ? COLOR_WARN : COLOR_OK;
    uint16_t sdColor = sdStats.card_mounted && sdStats.session_open ? COLOR_OK : COLOR_ERR;

    g_gfx->printf("%.1f f/s  Bus:%.1f%%  ", canStats.frames_per_sec, canStats.bus_load_pct);
    g_gfx->setTextColor(errColor);
    g_gfx->printf("Err:%lu BusOff:%lu  ",
                   static_cast<unsigned long>(canStats.bus_error_count),
                   static_cast<unsigned long>(canStats.bus_off_count));
    g_gfx->setTextColor(sdColor);
    if (!sdStats.card_mounted) {
        g_gfx->print("SD:NONE  ");
    } else {
        g_gfx->printf("SD:%.1fGB Q:%lu%%  ", sdStats.free_space_gb,
                       static_cast<unsigned long>(canStats.sd_queue_backlog_pct));
    }
    g_gfx->setTextColor(COLOR_TEXT);
    uint32_t upSec = millis() / 1000;
    g_gfx->printf("Up:%02lu:%02lu:%02lu  #%lu",
                   static_cast<unsigned long>(upSec / 3600),
                   static_cast<unsigned long>((upSec / 60) % 60),
                   static_cast<unsigned long>(upSec % 60),
                   static_cast<unsigned long>(sdStats.session_number));

    g_gfx->drawFastHLine(0, STATS_BAR_H - 1, g_gfx->width(), COLOR_DIM);
}

void drawEntryRow(int x, int y, const CanIdEntry &e) {
    g_gfx->setCursor(x, y);
    g_gfx->setTextColor(COLOR_HEADER);
    g_gfx->printf("%0*lX", e.extended ? 8 : 3, static_cast<unsigned long>(e.id));
    g_gfx->setTextColor(COLOR_TEXT);
    g_gfx->printf(" %d ", e.dlc);
    if (e.rtr) {
        g_gfx->print("RTR");
    } else {
        for (int i = 0; i < e.dlc; i++) {
            g_gfx->printf("%02X", e.last_data[i]);
        }
    }
    g_gfx->setTextColor(COLOR_DIM);
    g_gfx->printf(" %.1f%s", e.hz, hzColorLabel(e.hz));
}

void drawIdTable() {
    static CanIdEntry entries[CAN_ID_TABLE_MAX_ENTRIES];
    size_t total = CanIdTable::snapshot(entries, CAN_ID_TABLE_MAX_ENTRIES);

    size_t pageCount = (total == 0) ? 1 : (total + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;
    uint32_t nowMs = millis();
    if (nowMs - g_lastPageChangeMs >= PAGE_CYCLE_MS) {
        g_lastPageChangeMs = nowMs;
        g_currentPage = (g_currentPage + 1) % pageCount;
    }
    if (g_currentPage >= pageCount) g_currentPage = 0;

    g_gfx->fillRect(0, STATS_BAR_H, g_gfx->width(), g_gfx->height() - STATS_BAR_H, COLOR_BG);

    g_gfx->setTextSize(1);
    g_gfx->setTextColor(COLOR_DIM);
    g_gfx->setCursor(2, STATS_BAR_H + 1);
    g_gfx->printf("IDs seen: %d   page %d/%d", static_cast<int>(total),
                   static_cast<int>(g_currentPage + 1), static_cast<int>(pageCount));

    size_t startIdx = g_currentPage * ENTRIES_PER_PAGE;
    int y0 = STATS_BAR_H + ROW_H;
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        size_t idx = startIdx + i;
        if (idx >= total) break;
        int col = i / ENTRIES_PER_COL;
        int row = i % ENTRIES_PER_COL;
        int x = col * COL_W + 2;
        int y = y0 + row * ROW_H;
        drawEntryRow(x, y, entries[idx]);
    }
}

void displayTask(void *) {
    initHardware();
    g_lastPageChangeMs = millis();

    for (;;) {
        pollBootButton();
        CanCaptureStats canStats = CanCapture::getStats();
        SdLoggerStats sdStats = SdLogger::getStats();
        drawStatsBar(canStats, sdStats);
        drawIdTable();
        flushCanvasToPanel();
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}

} // namespace

namespace DisplayUi {

void begin() {
    xTaskCreatePinnedToCore(displayTask, "display", STACK_SIZE_DISPLAY, nullptr,
                             TASK_PRIO_DISPLAY, nullptr, CORE_SD_AND_DISPLAY);
}

} // namespace DisplayUi
