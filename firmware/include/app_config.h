#pragma once

#include <cstdint>

// --- TWAI / CAN ---
// 2014 Yaris Hybrid diagnostic (OBD-II) high-speed CAN bus, ISO 15765-4.
constexpr uint32_t CAN_BITRATE_BPS = 500000;

// --- Speed calibration ---
// The CAN SPEED signal reads low against the car's own speedometer: this
// screen showed 46 with the Yaris at 50 (and, with the correction wrongly
// inverted, 25 with the Yaris at 30). Scaled once in the decoder so every
// consumer (speed readout, session stats, 0-50/0-100 timer, G-meter) works
// from the same corrected value.
constexpr float SPEED_CORRECTION = 50.0f / 46.0f;

// --- FreeRTOS task priorities / core pinning ---
// Two-task split (no SD writer here, unlike obd-capture): CAN decode is the
// only latency-critical task, LVGL rendering is comparatively cheap and must
// never be starved for long enough to look janky, but can never be allowed to
// delay CAN reception either -- hence CAN stays highest-priority and on its
// own core, exactly like obd-capture.
constexpr int TASK_PRIO_CAN_RX = 20;
constexpr int TASK_PRIO_LVGL   = 10;

constexpr int CORE_CAN_RX = 1;
constexpr int CORE_LVGL   = 0;

constexpr uint32_t STACK_SIZE_CAN_RX = 4096;
constexpr uint32_t STACK_SIZE_LVGL   = 8192; // LVGL + canvas rasterization needs headroom

// --- Queues / buffers ---
constexpr int TWAI_DRIVER_RX_QUEUE_LEN = 100;
constexpr int TWAI_DRIVER_TX_QUEUE_LEN = 0; // never transmit (listen-only)

// --- LVGL timing ---
constexpr uint32_t LVGL_TICK_PERIOD_MS = 5;   // lv_tick_inc() cadence (esp_timer periodic cb)
constexpr uint32_t LVGL_TASK_DELAY_MS  = 16;  // ~60fps lv_timer_handler() loop
constexpr uint32_t UI_SYNC_INTERVAL_MS = 33;  // VehicleState -> widget push rate (~30Hz)

// --- Serial control commands ---
constexpr uint32_t SERIAL_BAUD = 115200;

// USB CAN dump (usb_dump.cpp). Its task shares core 1 with canRxTask, below
// it in priority: RX never waits on USB, frames queue up instead (~0.5 s of
// a busy bus at ~2000 frames/s), and are dropped -- and counted -- past that.
constexpr int TASK_PRIO_USB_DUMP = 5;
constexpr int CORE_USB_DUMP = 1;
constexpr uint32_t STACK_SIZE_USB_DUMP = 4096;
constexpr int USB_DUMP_QUEUE_LEN = 1024;               // x 32 B = 32 KB of RAM
constexpr uint32_t USB_DUMP_STALL_TIMEOUT_MS = 2000;   // nobody reading for this long -> stop

// Steering-wheel buttons (steering_buttons.cpp). Sampled from their own task
// rather than the UI loop: an LVGL frame during a screen transition can run
// long enough to swallow a whole short press. Above LVGL on its core, so a
// slow frame can't delay a sample; each one is a few ADC reads, then sleep.
constexpr int TASK_PRIO_STEERING = TASK_PRIO_LVGL + 1;
constexpr int CORE_STEERING = CORE_LVGL;
constexpr uint32_t STACK_SIZE_STEERING = 3072;
constexpr uint32_t STEERING_SAMPLE_PERIOD_MS = 10;
