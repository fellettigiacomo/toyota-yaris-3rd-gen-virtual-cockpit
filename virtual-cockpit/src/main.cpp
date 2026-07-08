// virtual-cockpit: LVGL instrument cluster for the Toyota Yaris Hybrid
// (3rd gen / XP130). Two FreeRTOS tasks (see include/app_config.h):
//   canRxTask (can_decoder.cpp) - TWAI listen-only RX + DBC decode, core 1
//   lvglTask  (display_driver.cpp) - LVGL rendering, core 0
//
// Build with `pio run -e demo` (DEMO_FAKE_DATA) to sweep the UI through
// fake values with no TWAI/vehicle connection at all -- see the plan doc's
// bring-up order. Normal builds (`pio run`) use live CAN.

#include <Arduino.h>
#include "app_config.h"
#include "rtc_clock.h"
#include "can_decoder.h"
#include "display_driver.h"

namespace {

void handleSerialCommand(const String &line) {
    if (line.startsWith("SETTIME ")) {
        time_t epoch = strtoul(line.c_str() + 8, nullptr, 10);
        RtcClock::set(epoch);
        Serial.printf("[main] RTC set to epoch %ld\n", static_cast<long>(epoch));
    } else if (line.length() > 0) {
        Serial.println("[main] unknown command. Known: SETTIME <unix_epoch>");
    }
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300); // let USB-CDC/serial monitor attach
    Serial.println("\n[main] virtual-cockpit starting");

#ifdef DEMO_FAKE_DATA
    Serial.println("[main] DEMO_FAKE_DATA build -- no CAN/vehicle connection needed");
#endif

    RtcClock::begin();
    if (!RtcClock::isValid()) {
        Serial.println("[main] RTC not set -- send 'SETTIME <unix_epoch_seconds>' over serial to set it. "
                        "Until then, the left-slot clock is left blank.");
    }

    CanDecoder::begin();
    DisplayDriver::begin();

    Serial.println("[main] all tasks started");
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        handleSerialCommand(line);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}
