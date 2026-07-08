#!/usr/bin/env python3
"""Nibble-level scan (since GEAR and TURN_SIGNALS turned out to be nibble/
sub-byte fields, not full bytes): look for a 4-bit or fewer sub-field with
2-8 distinct values that trends slowly and does NOT wrap repeatedly,
across both logs, restricted to IDs not yet explained."""
import sys
from collections import defaultdict
from parse_log import parse

EXPLAINED_IDS = {
    0x020, 0x024, 0x025, 0x0b4, 0x127, 0x1c4, 0x224, 0x230, 0x245, 0x260,
    0x399, 0x3b7, 0x611, 0x614, 0x618, 0x620, 0x622, 0x361, 0x49b, 0x3a0,
}


def wraps(vals):
    nd = len(set(vals))
    if nd < 4:
        return False
    span = max(vals) - min(vals)
    if span == 0:
        return False
    wrap_events = sum(1 for a, b in zip(vals, vals[1:]) if a - b > span * 0.6)
    return wrap_events >= 2


def main(path):
    frames = parse(path)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        if cid not in EXPLAINED_IDS:
            by_id[cid].append(data)

    for cid, datas in by_id.items():
        dlc = max(len(d) for d in datas)
        for bi in range(dlc):
            byte_vals = [d[bi] for d in datas if len(d) > bi]
            for nib_name, shift, mask in [("hi", 4, 0xF), ("lo", 0, 0xF)]:
                vals = [(v >> shift) & mask for v in byte_vals]
                nd = len(set(vals))
                if 2 <= nd <= 8 and not wraps(vals):
                    # require it to actually move around, not just 1-2 blips
                    changes = sum(1 for a, b in zip(vals, vals[1:]) if a != b)
                    if changes >= 3:
                        print(f"id={cid:#05x} byte={bi} nibble={nib_name}: distinct={sorted(set(vals))} n_changes={changes}")


if __name__ == "__main__":
    main(sys.argv[1])
