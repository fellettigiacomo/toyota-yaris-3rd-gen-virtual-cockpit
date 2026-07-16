#!/usr/bin/env python3
"""Search all (id, byte) pairs for a signal that ramps smoothly from a low
value toward a plateau over the drive duration, consistent with a coolant /
battery warm-up curve, or that hovers in a plausible 0-100 range for SOC%."""
import sys
from collections import defaultdict
from parse_log import parse


def main(path):
    frames = parse(path)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append((ts, data))

    candidates = []
    for cid, entries in by_id.items():
        dlc = max(len(d) for _, d in entries)
        for byte_idx in range(dlc):
            series = [(ts, d[byte_idx]) for ts, d in entries if len(d) > byte_idx]
            vals = [v for _, v in series]
            if len(set(vals)) < 15:
                continue
            # check overall trend: split into 10 time buckets, compare bucket means
            t0, t1 = series[0][0], series[-1][0]
            span = t1 - t0
            if span < 60:
                continue
            nb = 10
            buckets = [[] for _ in range(nb)]
            for ts, v in series:
                idx = min(nb - 1, int((ts - t0) / span * nb))
                buckets[idx].append(v)
            means = [sum(b) / len(b) for b in buckets if b]
            if len(means) < 8:
                continue
            total_change = means[-1] - means[0]
            # monotonic-ish trend: most consecutive diffs same sign as total_change
            diffs = [b - a for a, b in zip(means, means[1:])]
            if total_change == 0:
                continue
            same_sign = sum(1 for d in diffs if (d > 0) == (total_change > 0))
            frac_monotonic = same_sign / len(diffs)
            if frac_monotonic >= 0.75 and abs(total_change) >= 8:
                candidates.append((cid, byte_idx, total_change, means))

    candidates.sort(key=lambda c: -abs(c[2]))
    for cid, byte_idx, change, means in candidates[:30]:
        print(f"id={cid:#05x} byte={byte_idx} total_change={change:+.1f} bucket_means={[round(m,1) for m in means]}")


if __name__ == "__main__":
    main(sys.argv[1])
