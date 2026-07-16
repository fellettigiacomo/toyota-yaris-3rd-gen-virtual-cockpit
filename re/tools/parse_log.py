#!/usr/bin/env python3
"""Parse candump log into a list of (timestamp, can_id, data_bytes) tuples."""
import re

LINE_RE = re.compile(r"^\((\d+\.\d+)\)\s+\S+\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)\s*$")


def parse(path, drop_bogus_rtc_prefix=True):
    """Parse a candump log.

    The ESP32 logger has no RTC set at boot, so early lines carry a bogus
    huge epoch-like timestamp until the clock is corrected; after that the
    timestamp is a monotonic seconds counter starting near 0. When
    drop_bogus_rtc_prefix is True, frames before that one-time backwards
    jump are discarded so all returned timestamps are monotonic.
    """
    frames = []
    with open(path) as f:
        for line in f:
            m = LINE_RE.match(line.strip())
            if not m:
                continue
            ts = float(m.group(1))
            can_id = int(m.group(2), 16)
            data = bytes.fromhex(m.group(3))
            frames.append((ts, can_id, data))

    if drop_bogus_rtc_prefix:
        for i in range(1, len(frames)):
            if frames[i][0] < frames[i - 1][0] - 1:
                frames = frames[i:]
                break

    return frames


if __name__ == "__main__":
    import sys
    frames = parse(sys.argv[1])
    print(f"{len(frames)} frames, {len(set(f[1] for f in frames))} unique IDs")
