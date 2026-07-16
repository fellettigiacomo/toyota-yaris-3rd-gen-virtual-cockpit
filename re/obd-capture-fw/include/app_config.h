#pragma once

#include <cstddef>
#include <cstdint>

// --- TWAI / CAN ---
// 2014 Yaris Hybrid diagnostic (OBD-II) high-speed CAN bus, ISO 15765-4.
// Change this if a different sub-bus is tapped later (e.g. ~95.2 kbps body bus).
constexpr uint32_t CAN_BITRATE_BPS = 500000;
constexpr const char *CAN_IFACE_NAME = "can0"; // literal token written into candump lines

// --- FreeRTOS task priorities / core pinning ---
// (arduino-esp32 configMAX_PRIORITIES = 25)
constexpr int TASK_PRIO_CAN_RX     = 20; // highest of the four -- must never be starved
constexpr int TASK_PRIO_SD_WRITER  = 10;
constexpr int TASK_PRIO_USB_STREAM = 8;  // below SD, above display -- debug stream must not affect capture
constexpr int TASK_PRIO_DISPLAY    = 5;  // lowest -- a slow redraw must never affect capture

constexpr int CORE_CAN_RX          = 1;
constexpr int CORE_SD_AND_DISPLAY  = 0;

constexpr uint32_t STACK_SIZE_CAN_RX     = 4096;
constexpr uint32_t STACK_SIZE_SD_WRITER  = 4096;
constexpr uint32_t STACK_SIZE_USB_STREAM = 4096;
constexpr uint32_t STACK_SIZE_DISPLAY    = 4096;

// --- Queues / buffers ---
// Driver-internal TWAI RX queue (ISR-fed). Default is only 5 -- too small to
// survive any scheduling jitter, so it is raised here.
constexpr int TWAI_DRIVER_RX_QUEUE_LEN = 100;
constexpr int TWAI_DRIVER_TX_QUEUE_LEN = 0; // never transmit (listen-only)

// Application-level queue between canRxTask and sdWriterTask, sized to absorb
// an SD stall of ~300ms at a transient burst of ~2000 msg/s without dropping
// frames: 2000 * 0.3 = 600, rounded up with margin.
constexpr int APP_SD_QUEUE_DEPTH = 1024;

constexpr size_t SD_TEXT_BUFFER_BYTES = 16384; // formatted-text staging buffer
constexpr size_t SD_FLUSH_TRIGGER_BYTES = 12288; // flush when staging buffer reaches this
constexpr uint32_t SD_FLUSH_INTERVAL_MS = 1000;  // flush at least this often regardless of size
constexpr uint32_t SD_SYNC_INTERVAL_MS = 5000;    // fsync/commit FAT metadata this often (not every flush)
constexpr uint32_t SD_FREESPACE_POLL_MS = 10000;

// Application-level queue between canRxTask and usbStreamTask. Native
// USB-CDC easily outpaces a 500kbps CAN bus, so this only needs to absorb
// scheduling jitter, not a real stall like the SD card can have -- much
// shallower than APP_SD_QUEUE_DEPTH. A full queue just drops frames from the
// live view (see CanCaptureStats.usb_queue_backlog_pct); the SD log is
// unaffected since it's a separate queue.
constexpr int APP_USB_QUEUE_DEPTH = 256;

// --- Display ---
constexpr uint32_t DISPLAY_REFRESH_MS = 150; // ~6-7 Hz
constexpr int CAN_ID_TABLE_MAX_ENTRIES = 300; // typical vehicle-bus unique-ID ceiling

// --- Serial control commands ---
constexpr uint32_t SERIAL_BAUD = 115200;

// Periodic compact status line printed to Serial (same fields as the display's
// stats bar), so capture progress is visible even without a working screen.
constexpr uint32_t SERIAL_STATUS_INTERVAL_MS = 1000;
