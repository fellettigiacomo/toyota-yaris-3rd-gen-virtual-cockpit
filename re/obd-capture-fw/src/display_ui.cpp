#include "display_ui.h"
#include "board_pins.h"
#include "app_config.h"
#include "can_capture.h"
#include "can_id_table.h"
#include "sd_logger.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "freertos/task.h"

namespace {

Arduino_DataBus *g_bus = nullptr;
Arduino_GFX *g_gfx = nullptr;

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

void initHardware() {
    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_LCD_BACKLIGHT, HIGH);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    g_bus = new Arduino_ESP32QSPI(PIN_LCD_QSPI_CS, PIN_LCD_QSPI_SCK,
                                   PIN_LCD_QSPI_D0, PIN_LCD_QSPI_D1,
                                   PIN_LCD_QSPI_D2, PIN_LCD_QSPI_D3);
    g_gfx = new Arduino_AXS15231B(g_bus, PIN_LCD_RESET, LCD_ROTATION, true,
                                   LCD_PANEL_NATIVE_WIDTH, LCD_PANEL_NATIVE_HEIGHT);
    g_gfx->begin();
    g_gfx->fillScreen(COLOR_BG);
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
    g_gfx->printf("ID visti: %d   pagina %d/%d", static_cast<int>(total),
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
