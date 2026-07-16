#include "sd_logger.h"
#include "board_pins.h"
#include "app_config.h"
#include "rtc_clock.h"
#include "candump_format.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <cstring>
#include "freertos/task.h"
#include "esp_timer.h"

namespace {

QueueHandle_t g_queue = nullptr;
volatile bool g_stopRequested = false;

Preferences g_prefs;
constexpr const char *PREFS_NAMESPACE = "obdcapture";
constexpr const char *PREFS_SESSION_KEY = "session_no";

bool g_cardMounted = false;
uint32_t g_sessionNumber = 0;
File g_logFile;
File g_metaFile;
bool g_sessionOpen = false;

uint64_t g_sessionStartMonotonicUs = 0;
time_t g_sessionStartEpoch = 0;
bool g_sessionRtcValid = false;

char g_textBuf[SD_TEXT_BUFFER_BYTES];
size_t g_textBufLen = 0;
uint32_t g_framesWritten = 0;
uint32_t g_lastFlushMs = 0;
uint32_t g_lastSyncMs = 0;
uint32_t g_lastFreeSpacePollMs = 0;
float g_freeSpaceGb = 0;

CandumpTimeBase sessionTimeBase() {
    return CandumpTimeBase{g_sessionRtcValid, g_sessionStartEpoch, g_sessionStartMonotonicUs};
}

void loadSessionNumber() {
    g_prefs.begin(PREFS_NAMESPACE, false);
    g_sessionNumber = g_prefs.getUInt(PREFS_SESSION_KEY, 0) + 1;
    g_prefs.putUInt(PREFS_SESSION_KEY, g_sessionNumber);
    g_prefs.end();
}

void openSession() {
    g_sessionStartMonotonicUs = esp_timer_get_time();
    g_sessionRtcValid = RtcClock::isValid();
    g_sessionStartEpoch = g_sessionRtcValid ? RtcClock::now() : 0;

    char stamp[24];
    if (g_sessionRtcValid) {
        struct tm t;
        gmtime_r(&g_sessionStartEpoch, &t);
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        strcpy(stamp, "RTCUNSET");
    }

    if (!SD_MMC.exists("/CANLOGS")) {
        SD_MMC.mkdir("/CANLOGS");
    }

    char logPath[64];
    char metaPath[64];
    snprintf(logPath, sizeof(logPath), "/CANLOGS/session_%04lu_%s.log",
              static_cast<unsigned long>(g_sessionNumber), stamp);
    snprintf(metaPath, sizeof(metaPath), "/CANLOGS/session_%04lu_%s.meta",
              static_cast<unsigned long>(g_sessionNumber), stamp);

    g_logFile = SD_MMC.open(logPath, FILE_WRITE);
    g_metaFile = SD_MMC.open(metaPath, FILE_WRITE);
    g_framesWritten = 0;
    g_textBufLen = 0;
    g_sessionOpen = g_logFile && g_metaFile;

    if (g_sessionOpen) {
        g_metaFile.printf("session_number=%lu\n", static_cast<unsigned long>(g_sessionNumber));
        g_metaFile.printf("rtc_valid=%d\n", g_sessionRtcValid ? 1 : 0);
        g_metaFile.printf("start_epoch=%ld\n", static_cast<long>(g_sessionStartEpoch));
        g_metaFile.printf("can_bitrate_bps=%u\n", static_cast<unsigned>(CAN_BITRATE_BPS));
        g_metaFile.flush();
        Serial.printf("[sd_logger] session %lu opened: %s\n",
                       static_cast<unsigned long>(g_sessionNumber), logPath);
    } else {
        Serial.println("[sd_logger] ERROR: failed to open session files");
    }
}

void flushTextBuffer() {
    if (!g_sessionOpen || g_textBufLen == 0) return;
    g_logFile.write(reinterpret_cast<const uint8_t *>(g_textBuf), g_textBufLen);
    g_textBufLen = 0;
    g_lastFlushMs = millis();
}

void closeSession(uint32_t busErrorCount, uint32_t busOffCount, uint32_t droppedCount) {
    if (!g_sessionOpen) return;
    flushTextBuffer();
    g_logFile.flush();
    g_logFile.close();
    g_metaFile.printf("frames_written=%lu\n", static_cast<unsigned long>(g_framesWritten));
    g_metaFile.printf("bus_error_count=%lu\n", static_cast<unsigned long>(busErrorCount));
    g_metaFile.printf("bus_off_count=%lu\n", static_cast<unsigned long>(busOffCount));
    g_metaFile.printf("dropped_frames=%lu\n", static_cast<unsigned long>(droppedCount));
    g_metaFile.flush();
    g_metaFile.close();
    g_sessionOpen = false;
    Serial.printf("[sd_logger] session %lu closed, %lu frames written\n",
                   static_cast<unsigned long>(g_sessionNumber),
                   static_cast<unsigned long>(g_framesWritten));
}

void pollFreeSpace() {
    uint32_t nowMs = millis();
    if (nowMs - g_lastFreeSpacePollMs < SD_FREESPACE_POLL_MS) return;
    g_lastFreeSpacePollMs = nowMs;
    uint64_t total = SD_MMC.totalBytes();
    uint64_t used = SD_MMC.usedBytes();
    g_freeSpaceGb = (total > used) ? (total - used) / (1024.0f * 1024.0f * 1024.0f) : 0;
}

void sdWriterTask(void *) {
    g_cardMounted = SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0) &&
                    SD_MMC.begin("/sdcard", true /* 1-bit mode */, false /* do not format on failure */);
    if (!g_cardMounted) {
        Serial.println("[sd_logger] ERROR: SD_MMC.begin() failed -- no card, or wiring/mode mismatch");
    } else {
        loadSessionNumber();
        openSession();
    }

    CanFrameRecord rec;
    for (;;) {
        bool got = xQueueReceive(g_queue, &rec, pdMS_TO_TICKS(20)) == pdTRUE;
        if (got && g_cardMounted && g_sessionOpen) {
            char line[64];
            size_t len = formatCandumpLine(line, sizeof(line), rec, sessionTimeBase());
            if (len > 0) {
                if (g_textBufLen + len >= SD_TEXT_BUFFER_BYTES) {
                    flushTextBuffer();
                }
                memcpy(g_textBuf + g_textBufLen, line, len);
                g_textBufLen += len;
                g_framesWritten++;
            }
        }

        if (g_cardMounted && g_sessionOpen) {
            uint32_t nowMs = millis();
            if (g_textBufLen >= SD_FLUSH_TRIGGER_BYTES ||
                (g_textBufLen > 0 && (nowMs - g_lastFlushMs) >= SD_FLUSH_INTERVAL_MS)) {
                flushTextBuffer();
            }
            if (nowMs - g_lastSyncMs >= SD_SYNC_INTERVAL_MS) {
                g_logFile.flush(); // commits to the underlying FS layer
                g_lastSyncMs = nowMs;
            }
            pollFreeSpace();
        }

        if (g_stopRequested && g_sessionOpen) {
            // Drain whatever is left in the queue before closing.
            while (xQueueReceive(g_queue, &rec, 0) == pdTRUE) {
                char line[64];
                size_t len = formatCandumpLine(line, sizeof(line), rec, sessionTimeBase());
                if (len > 0) {
                    if (g_textBufLen + len >= SD_TEXT_BUFFER_BYTES) flushTextBuffer();
                    memcpy(g_textBuf + g_textBufLen, line, len);
                    g_textBufLen += len;
                    g_framesWritten++;
                }
            }
            closeSession(0, 0, 0);
            g_stopRequested = false;
            loadSessionNumber();
            openSession();
        }
    }
}

} // namespace

namespace SdLogger {

QueueHandle_t begin() {
    g_queue = xQueueCreate(APP_SD_QUEUE_DEPTH, sizeof(CanFrameRecord));
    xTaskCreatePinnedToCore(sdWriterTask, "sdWriter", STACK_SIZE_SD_WRITER, nullptr,
                             TASK_PRIO_SD_WRITER, nullptr, CORE_SD_AND_DISPLAY);
    return g_queue;
}

void requestStop() {
    g_stopRequested = true;
}

SdLoggerStats getStats() {
    SdLoggerStats s{};
    s.card_mounted = g_cardMounted;
    s.session_open = g_sessionOpen;
    s.free_space_gb = g_freeSpaceGb;
    s.session_number = g_sessionNumber;
    s.frames_written = g_framesWritten;
    return s;
}

} // namespace SdLogger
