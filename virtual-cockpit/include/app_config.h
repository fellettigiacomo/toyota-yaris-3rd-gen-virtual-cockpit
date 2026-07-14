#pragma once

#include <cstdint>

// --- TWAI / CAN ---
// 2014 Yaris Hybrid diagnostic (OBD-II) high-speed CAN bus, ISO 15765-4.
constexpr uint32_t CAN_BITRATE_BPS = 500000;

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

// --- MODE button (0x4AC) ---
// 0x4AC broadcasts every 500ms even when the button is idle, so an active
// mode_button latch that hasn't been refreshed by ANY 0x4AC frame for over
// 3 periods means the idle-again frames were lost, not that the button is
// still held. Clearing the latch then keeps a lost idle transition from
// suppressing the next real press's rising edge.
constexpr uint32_t MODE_BUTTON_STALE_MS = 1600;

// --- LVGL timing ---
constexpr uint32_t LVGL_TICK_PERIOD_MS = 5;   // lv_tick_inc() cadence (esp_timer periodic cb)
constexpr uint32_t LVGL_TASK_DELAY_MS  = 16;  // ~60fps lv_timer_handler() loop
constexpr uint32_t UI_SYNC_INTERVAL_MS = 33;  // VehicleState -> widget push rate (~30Hz)

// --- Serial control commands ---
constexpr uint32_t SERIAL_BAUD = 115200;
