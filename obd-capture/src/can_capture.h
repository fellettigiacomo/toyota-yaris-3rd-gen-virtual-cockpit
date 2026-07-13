#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// One captured CAN frame, timestamped as microseconds since boot
// (esp_timer_get_time()). Converted to an absolute candump timestamp only at
// the point of writing to SD (see sd_logger), so canRxTask never touches RTC/I2C.
struct CanFrameRecord {
    uint64_t timestamp_us;
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    bool extended;
    bool rtr;
};

struct CanCaptureStats {
    float frames_per_sec;
    float bus_load_pct;
    uint32_t bus_error_count;
    uint32_t bus_off_count;
    uint32_t rx_queue_full_count;
    uint32_t rx_fifo_overrun_count;
    uint32_t total_frames;
    uint32_t sd_queue_backlog_pct;
    uint32_t usb_queue_backlog_pct;
};

namespace CanCapture {

// Installs and starts the TWAI driver in listen-only mode (accept-all filter,
// CAN_BITRATE_BPS from app_config.h), creates the SD-log queue, and spawns
// canRxTask pinned to CORE_CAN_RX at TASK_PRIO_CAN_RX.
//
// sdQueueOut receives every captured frame; the caller (sd_logger) owns
// consuming it. usbQueueOut (may be nullptr) similarly receives every frame
// for the caller (usb_stream) to consume; it is a second, independent fan-out
// so a full/absent USB queue never affects SD logging or vice versa. Must be
// called after Serial.begin().
void begin(QueueHandle_t sdQueueOut, QueueHandle_t usbQueueOut);

// Snapshot of rolling capture statistics, safe to call from displayTask.
CanCaptureStats getStats();

} // namespace CanCapture
