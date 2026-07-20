// virtual-cockpit: LVGL instrument cluster for the Toyota Yaris Hybrid
// (3rd gen / XP130). Two FreeRTOS tasks (see include/app_config.h):
//   canRxTask (can_decoder.cpp) - TWAI listen-only RX + DBC decode, core 1
//   lvglTask  (display_driver.cpp) - LVGL rendering, core 0
//
// Build with `pio run -e demo` (DEMO_FAKE_DATA) to sweep the UI through
// fake values with no TWAI/vehicle connection at all. Normal builds
// (`pio run`) use live CAN.

#include <Arduino.h>
#include "app_config.h"
#include "can_decoder.h"
#include "display_driver.h"

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300); // let USB-CDC/serial monitor attach
    Serial.println("\n[main] virtual-cockpit starting");

#ifdef DEMO_FAKE_DATA
    Serial.println("[main] DEMO_FAKE_DATA build -- no CAN/vehicle connection needed");
#endif

    CanDecoder::begin();
    DisplayDriver::begin();

    Serial.println("[main] all tasks started");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(50));
}
