#include "candump_format.h"
#include "app_config.h"

#include <cstdio>

size_t formatCandumpLine(char *out, size_t outLen, const CanFrameRecord &rec,
                          const CandumpTimeBase &base) {
    uint64_t absUs;
    if (base.rtc_valid) {
        absUs = static_cast<uint64_t>(base.base_epoch) * 1000000ULL +
                (rec.timestamp_us - base.base_monotonic_us);
    } else {
        absUs = rec.timestamp_us - base.base_monotonic_us;
    }
    uint32_t sec = absUs / 1000000ULL;
    uint32_t usec = absUs % 1000000ULL;

    int n = snprintf(out, outLen, "(%lu.%06lu) %s %0*x#",
                      static_cast<unsigned long>(sec), static_cast<unsigned long>(usec),
                      CAN_IFACE_NAME, rec.extended ? 8 : 3, rec.id);
    if (n < 0 || static_cast<size_t>(n) >= outLen) return 0;
    size_t pos = static_cast<size_t>(n);

    if (rec.rtr) {
        if (pos + 1 >= outLen) return 0;
        out[pos++] = 'R';
    } else {
        for (int i = 0; i < rec.dlc && pos + 2 < outLen; i++) {
            int written = snprintf(out + pos, outLen - pos, "%02x", rec.data[i]);
            if (written != 2) return 0;
            pos += 2;
        }
    }
    if (pos + 1 >= outLen) return 0;
    out[pos++] = '\n';
    return pos;
}
