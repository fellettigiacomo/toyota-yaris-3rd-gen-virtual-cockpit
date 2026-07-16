#include "usb_stream.h"
#include "candump_format.h"
#include "app_config.h"
#include "rtc_clock.h"

#include <Arduino.h>
#include "esp_timer.h"

namespace {

QueueHandle_t g_queue = nullptr;
volatile bool g_enabled = false;
CandumpTimeBase g_timeBase{};

void usbStreamTask(void *) {
    CanFrameRecord rec;
    for (;;) {
        bool got = xQueueReceive(g_queue, &rec, pdMS_TO_TICKS(50)) == pdTRUE;
        if (got && g_enabled) {
            char line[64];
            size_t len = formatCandumpLine(line, sizeof(line), rec, g_timeBase);
            if (len > 0) {
                Serial.write(reinterpret_cast<const uint8_t *>(line), len);
            }
        }
    }
}

} // namespace

namespace UsbStream {

QueueHandle_t begin() {
    g_queue = xQueueCreate(APP_USB_QUEUE_DEPTH, sizeof(CanFrameRecord));
    xTaskCreatePinnedToCore(usbStreamTask, "usbStream", STACK_SIZE_USB_STREAM, nullptr,
                             TASK_PRIO_USB_STREAM, nullptr, CORE_SD_AND_DISPLAY);
    return g_queue;
}

void setEnabled(bool enabled) {
    if (enabled) {
        // Fresh time base every time streaming starts, so a SETTIME issued
        // after boot is reflected immediately instead of only at next reboot.
        g_timeBase.rtc_valid = RtcClock::isValid();
        g_timeBase.base_monotonic_us = esp_timer_get_time();
        g_timeBase.base_epoch = g_timeBase.rtc_valid ? RtcClock::now() : 0;
        xQueueReset(g_queue);
    }
    g_enabled = enabled;
}

bool isEnabled() {
    return g_enabled;
}

} // namespace UsbStream
