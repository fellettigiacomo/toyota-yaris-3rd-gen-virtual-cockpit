#!/usr/bin/env python3
"""For each CAN ID, print per-byte distinct value count + a classification
hint, to help spot enum/monotonic/oscillating/constant candidates."""
import sys
from collections import defaultdict
from parse_log import parse


def classify(values):
    distinct = sorted(set(values))
    nd = len(distinct)
    if nd == 1:
        return "const", nd, distinct
    diffs = [b - a for a, b in zip(values, values[1:])]
    nz = [d for d in diffs if d != 0]
    changes = len(nz)
    n_pos = sum(1 for d in nz if d > 0)
    n_neg = sum(1 for d in nz if d < 0)
    if changes and (n_neg == 0 or n_pos == 0):
        tag = "monotonic"
    elif nd <= 8:
        tag = "enum"
    else:
        tag = "oscillating"
    return tag, nd, distinct[:10]


def main(path):
    frames = parse(path)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append(data)

    for cid in sorted(by_id):
        datas = by_id[cid]
        dlc = max(len(d) for d in datas)
        print(f"\n=== ID {cid:#05x} ({cid}) dlc={dlc} n={len(datas)} ===")
        for byte_idx in range(dlc):
            vals = [d[byte_idx] for d in datas if len(d) > byte_idx]
            tag, nd, sample = classify(vals)
            print(f"  byte[{byte_idx}]: {tag:10s} distinct={nd:4d} sample={sample}")


if __name__ == "__main__":
    main(sys.argv[1])
