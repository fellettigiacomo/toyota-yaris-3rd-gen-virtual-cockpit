#include "can_capture.h"
#include "can_id_table.h"
#include "board_pins.h"
#include "app_config.h"

#include <Arduino.h>
#include <cstring>
#include "driver/twai.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {

QueueHandle_t g_sdQueue = nullptr;

// Stats, updated only by canRxTask, read via getStats() from displayTask.
// Single-writer/many-reader plain values are fine here: a torn read at worst
// shows a stale/half-updated stat for one 150ms display refresh, which is an
// acceptable tradeoff against taking a lock on the hot RX path.
volatile float g_framesPerSec = 0;
volatile float g_busLoadPct = 0;
volatile uint32_t g_busErrorCount = 0;
volatile uint32_t g_busOffCount = 0;
volatile uint32_t g_rxQueueFullCount = 0;
volatile uint32_t g_rxFifoOverrunCount = 0;
volatile uint32_t g_totalFrames = 0;

void twaiInit() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)PIN_TWAI_TX, (gpio_num_t)PIN_TWAI_RX, TWAI_MODE_LISTEN_ONLY);
    g_config.rx_queue_len = TWAI_DRIVER_RX_QUEUE_LEN;
    g_config.tx_queue_len = TWAI_DRIVER_TX_QUEUE_LEN;
    g_config.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                              TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_ERR_PASS |
                              TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    static_assert(CAN_BITRATE_BPS == 500000,
                  "t_config above is hardcoded for 500kbps -- update if CAN_BITRATE_BPS changes");
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());

    Serial.println("[can_capture] TWAI started: 500kbps, LISTEN_ONLY, accept-all filter");
}

void pollAlerts() {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) != ESP_OK) {
        return;
    }
    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) g_rxQueueFullCount++;
    if (alerts & TWAI_ALERT_RX_FIFO_OVERRUN) g_rxFifoOverrunCount++;
    if (alerts & TWAI_ALERT_BUS_ERROR) g_busErrorCount++;
    if (alerts & TWAI_ALERT_BUS_OFF) {
        g_busOffCount++;
        Serial.println("[can_capture] WARNING: TWAI bus-off detected, restarting driver");
        twai_initiate_recovery();
    }
}

void canRxTask(void *) {
    twaiInit();
    CanIdTable::init();

    uint64_t windowStartUs = esp_timer_get_time();
    uint32_t framesInWindow = 0;
    uint64_t bitsInWindow = 0;

    for (;;) {
        twai_message_t msg;
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            CanFrameRecord rec{};
            rec.timestamp_us = esp_timer_get_time();
            rec.id = msg.identifier;
            rec.dlc = msg.data_length_code;
            rec.extended = msg.extd;
            rec.rtr = msg.rtr;
            if (!rec.rtr) {
                memcpy(rec.data, msg.data, rec.dlc);
            }

            CanIdTable::update(rec);

            if (g_sdQueue != nullptr) {
                // Non-blocking: if the SD queue is momentarily full, drop the
                // frame rather than stall CAN reception. A full queue is
                // itself visible via getStats().sd_queue_backlog_pct.
                xQueueSend(g_sdQueue, &rec, 0);
            }

            g_totalFrames++;
            framesInWindow++;
            // Bus-load approximation: ~47 bits of overhead (SOF+arb+control+
            // CRC+ACK+EOF, ignoring bit-stuffing) plus 8 bits per data byte.
            bitsInWindow += 47 + 8 * rec.dlc;
        }

        pollAlerts();

        uint64_t nowUs = esp_timer_get_time();
        uint64_t elapsedUs = nowUs - windowStartUs;
        if (elapsedUs >= 1000000) {
            g_framesPerSec = framesInWindow * 1000000.0f / elapsedUs;
            g_busLoadPct = (bitsInWindow * 100.0f / CAN_BITRATE_BPS) * 1000000.0f / elapsedUs;
            framesInWindow = 0;
            bitsInWindow = 0;
            windowStartUs = nowUs;
        }
    }
}

} // namespace

namespace CanCapture {

void begin(QueueHandle_t sdQueueOut) {
    g_sdQueue = sdQueueOut;
    xTaskCreatePinnedToCore(canRxTask, "canRx", STACK_SIZE_CAN_RX, nullptr,
                             TASK_PRIO_CAN_RX, nullptr, CORE_CAN_RX);
}

CanCaptureStats getStats() {
    CanCaptureStats s{};
    s.frames_per_sec = g_framesPerSec;
    s.bus_load_pct = g_busLoadPct;
    s.bus_error_count = g_busErrorCount;
    s.bus_off_count = g_busOffCount;
    s.rx_queue_full_count = g_rxQueueFullCount;
    s.rx_fifo_overrun_count = g_rxFifoOverrunCount;
    s.total_frames = g_totalFrames;
    if (g_sdQueue != nullptr) {
        s.sd_queue_backlog_pct =
            (uxQueueMessagesWaiting(g_sdQueue) * 100) / APP_SD_QUEUE_DEPTH;
    }
    return s;
}

} // namespace CanCapture
