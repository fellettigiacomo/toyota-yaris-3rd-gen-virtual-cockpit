#pragma once

// Bring-up of the AXS15231B display (via Arduino_GFX) and a lightweight,
// non-LVGL status UI: a stats bar (frames/s, bus load %, error counters, SD
// status, uptime) plus a scrolling two-column table of every unique CAN ID
// seen so far with its last payload and update frequency -- essentially
// "candump on screen", useful directly for reverse-engineering IDs.
//
// The physical BOOT button (GPIO0) is used as the "stop current capture
// session / start a new one" control, instead of the capacitive touch
// controller, to avoid depending on the AXS15231B touch protocol.
namespace DisplayUi {

// Initializes the panel and spawns displayTask pinned to CORE_SD_AND_DISPLAY
// at TASK_PRIO_DISPLAY.
void begin();

} // namespace DisplayUi
