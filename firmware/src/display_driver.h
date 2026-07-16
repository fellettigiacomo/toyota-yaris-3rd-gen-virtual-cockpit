#pragma once

// Bring-up of the AXS15231B display (via ESP-IDF esp_lcd + Waveshare's own
// panel driver, see src/axs15231b/ -- not Arduino_GFX) bridged to LVGL, and
// the LVGL task that owns all lv_* calls (LVGL itself is not thread-safe --
// every lv_* call in this firmware happens from lvglTask, never from
// canRxTask or the Arduino loop()).
namespace DisplayDriver {

// Initializes the panel, sets up the LVGL display driver + draw buffers
// (two full-screen buffers in PSRAM), builds the cockpit UI widget tree, and
// spawns lvglTask pinned to CORE_LVGL at TASK_PRIO_LVGL. lvglTask calls
// lv_timer_handler() continuously and periodically (UI_SYNC_INTERVAL_MS)
// pushes a fresh CanDecoder::getSnapshot() into the widget tree.
void begin();

} // namespace DisplayDriver
