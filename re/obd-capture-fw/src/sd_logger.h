#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "can_capture.h"

struct SdLoggerStats {
    bool card_mounted;
    bool session_open;
    float free_space_gb;
    uint32_t session_number;
    uint32_t frames_written;
};

namespace SdLogger {

// Mounts the SD card (SDMMC 1-bit mode), starts a new numbered session file,
// and spawns sdWriterTask pinned to CORE_SD_AND_DISPLAY at TASK_PRIO_SD_WRITER.
// Returns the queue that canRxTask should push CanFrameRecord items onto.
QueueHandle_t begin();

// Requests a clean, safe finalization of the current session (drains the
// queue, flushes, closes the file, writes the .meta sidecar). Safe to call
// from displayTask (e.g. on a BOOT-button press). Non-blocking; the actual
// close happens inside sdWriterTask.
void requestStop();

SdLoggerStats getStats();

} // namespace SdLogger
