#pragma once

#include <cstddef>
#include <ctime>
#include "can_capture.h"

// Shared candump-line formatter used by both sd_logger (writes to SD) and
// usb_stream (writes to Serial/USB-CDC), so the on-disk and live-streamed
// formats are byte-for-byte identical.

struct CandumpTimeBase {
    bool rtc_valid;
    time_t base_epoch;          // wall-clock epoch at base_monotonic_us, if rtc_valid
    uint64_t base_monotonic_us; // esp_timer_get_time() reading paired with base_epoch
};

// Formats one candump-compatible line:
//   (<seconds>.<microseconds>) can0 <id_hex>#<data_hex_or_R>\n
// ID is zero-padded to 3 hex digits (standard) or 8 hex digits (extended),
// lowercase, matching SocketCAN's own candump/.log convention exactly so
// SavvyCAN/python-can/cantools import it without any massaging.
// Returns the number of bytes written, or 0 if out was too small.
size_t formatCandumpLine(char *out, size_t outLen, const CanFrameRecord &rec,
                          const CandumpTimeBase &base);
