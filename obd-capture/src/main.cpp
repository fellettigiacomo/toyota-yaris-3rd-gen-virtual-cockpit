// obd-capture: passive CAN bus logger for the Toyota Yaris Hybrid (3rd gen).
//
// Three FreeRTOS tasks (see app_config.h for priorities/cores):
//   canRxTask    (can_capture.cpp)  - TWAI listen-only RX, highest priority, core 1
//   sdWriterTask (sd_logger.cpp)    - buffered candump-format SD logging, core 0
//   displayTask  (display_ui.cpp)  - live ID table + stats bar, lowest priority, core 0
//
// canRxTask never touches SD or display APIs, so an SD stall or slow redraw
// can never cause a dropped CAN frame.

#include <Arduino.h>
#include "app_config.h"
#include "rtc_clock.h"
#include "sd_logger.h"
#include "can_capture.h"
#include "display_ui.h"

namespace {

void handleSerialCommand(const String &line) {
    if (line.startsWith("SETTIME ")) {
        time_t epoch = strtoul(line.c_str() + 8, nullptr, 10);
        RtcClock::set(epoch);
        Serial.printf("[main] RTC set to epoch %ld\n", static_cast<long>(epoch));
    } else if (line == "STOP") {
        SdLogger::requestStop();
        Serial.println("[main] capture session stop requested");
    } else if (line.length() > 0) {
        Serial.println("[main] unknown command. Known: SETTIME <unix_epoch>, STOP");
    }
}

// Compact one-line status, same fields as display_ui.cpp's stats bar, printed
// periodically so capture progress is visible over serial even when the
// display isn't working.
void printStatusLine() {
    CanCaptureStats canStats = CanCapture::getStats();
    SdLoggerStats sdStats = SdLogger::getStats();
    uint32_t upSec = millis() / 1000;

    Serial.printf("[status] %.1ff/s Bus:%.1f%% Err:%lu BusOff:%lu SD:%s",
                  canStats.frames_per_sec, canStats.bus_load_pct,
                  static_cast<unsigned long>(canStats.bus_error_count),
                  static_cast<unsigned long>(canStats.bus_off_count),
                  sdStats.card_mounted ? (sdStats.session_open ? "open" : "mounted") : "NONE");
    if (sdStats.card_mounted) {
        Serial.printf(" %.1fGBfree Q:%lu%% session:%lu frames:%lu",
                      sdStats.free_space_gb,
                      static_cast<unsigned long>(canStats.sd_queue_backlog_pct),
                      static_cast<unsigned long>(sdStats.session_number),
                      static_cast<unsigned long>(sdStats.frames_written));
    }
    Serial.printf(" Up:%02lu:%02lu:%02lu\n",
                  static_cast<unsigned long>(upSec / 3600),
                  static_cast<unsigned long>((upSec / 60) % 60),
                  static_cast<unsigned long>(upSec % 60));
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300); // let USB-CDC/serial monitor attach
    Serial.println("\n[main] obd-capture starting");

    RtcClock::begin();
    if (!RtcClock::isValid()) {
        Serial.println("[main] RTC not set -- send 'SETTIME <unix_epoch_seconds>' over serial to set it. "
                        "Until then, log timestamps are relative to boot, not wall-clock.");
    }

    QueueHandle_t sdQueue = SdLogger::begin();
    CanCapture::begin(sdQueue);
    DisplayUi::begin();

    Serial.println("[main] all tasks started");
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        handleSerialCommand(line);
    }

    static uint32_t lastStatusMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastStatusMs >= SERIAL_STATUS_INTERVAL_MS) {
        lastStatusMs = nowMs;
        printStatusLine();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
}
