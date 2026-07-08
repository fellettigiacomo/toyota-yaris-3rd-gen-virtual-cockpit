#!/usr/bin/env python3
"""Detect bus-wide transmission gaps: 10-second buckets with an abnormally
low count of unique CAN IDs / frames compared to the rest of the log.

Written after noticing a ~100s window in session_0047 (t=149.8-249.3s) where
nearly all 100 IDs stopped appearing simultaneously, with a partial then full
recovery. A genuine CAN bus silence that long while driving is implausible
(SPEED/RPM/GEAR are always broadcasting); this is far more consistent with
the ESP32 logger stalling (buffer/SD-card backlog under ~800 frames/s
sustained throughput) than with a real vehicle-bus event. Use this script on
any new capture to catch the same artifact before reading too much into
"missing" IDs during that window.
"""
import sys
from collections import defaultdict
from parse_log import parse


def main(path, bucket_s=10):
    frames = parse(path)
    t0 = frames[0][0]
    buckets_ids = defaultdict(set)
    buckets_n = defaultdict(int)
    for ts, cid, d in frames:
        b = int((ts - t0) // bucket_s)
        buckets_ids[b].add(cid)
        buckets_n[b] += 1

    all_buckets = sorted(buckets_ids)
    typical = sorted(len(buckets_ids[b]) for b in all_buckets)[len(all_buckets) // 2]
    print(f"{path}: median unique-IDs/bucket={typical}, flagging buckets under 80% of that\n")
    for b in all_buckets:
        n_ids = len(buckets_ids[b])
        if n_ids < 0.8 * typical:
            print(f"  t={b*bucket_s:5d}-{b*bucket_s+bucket_s:5d}s: "
                  f"{n_ids:3d} unique IDs (vs median {typical}), {buckets_n[b]:5d} frames  <-- GAP")


if __name__ == "__main__":
    main(sys.argv[1])
