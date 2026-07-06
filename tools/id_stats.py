#!/usr/bin/env python3
"""Per-ID statistics: count, dlc, period, byte-level distinct-value counts,
classification hints (constant/monotonic/oscillating/enum)."""
import sys
from collections import defaultdict
from parse_log import parse


def classify_byte(values):
    """values: list of ints (0-255) in temporal order for one byte position."""
    distinct = sorted(set(values))
    n_distinct = len(distinct)
    if n_distinct == 1:
        return "constant", n_distinct
    diffs = [b - a for a, b in zip(values, values[1:])]
    nonzero = [d for d in diffs if d != 0]
    if not nonzero:
        return "constant", n_distinct
    n_pos = sum(1 for d in nonzero if d > 0)
    n_neg = sum(1 for d in nonzero if d < 0)
    if n_neg == 0 or n_pos == 0:
        # monotonic (allowing wraparound noise ignored for now)
        return "monotonic", n_distinct
    if n_distinct <= 8:
        return "enum", n_distinct
    return "oscillating", n_distinct


def main(path):
    frames = parse(path)
    t0 = frames[0][0]
    tN = frames[-1][0]
    duration = tN - t0

    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append((ts, data))

    print(f"Duration: {duration:.1f}s, {len(frames)} frames, {len(by_id)} unique IDs\n")
    print(f"{'ID':>5} {'hex':>5} {'count':>7} {'Hz':>7} {'dlc(s)':>10}")
    for cid in sorted(by_id):
        entries = by_id[cid]
        dlcs = sorted(set(len(d) for _, d in entries))
        hz = len(entries) / duration
        print(f"{cid:5d} {cid:#05x} {len(entries):7d} {hz:7.2f} {str(dlcs):>10}")


if __name__ == "__main__":
    main(sys.argv[1])
