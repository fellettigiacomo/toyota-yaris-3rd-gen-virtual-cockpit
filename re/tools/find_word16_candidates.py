#!/usr/bin/env python3
"""Scan every (id, byte-pair) combination -- adjacent big-endian, adjacent
little-endian, and non-adjacent skip-1 pairs -- for a bounded, non-wrapping,
slowly-varying 16-bit-ish candidate that has NOT already been explained by a
known signal. Meant to catch a SOC%/range-style value that a single-byte or
nibble scan would miss (e.g. because it needs >8 bits of resolution, or lives
in two bytes that aren't strictly adjacent in the DBC-conventional order).

Run against BOTH logs and intersect results by hand -- this script only
reports one log at a time so a false positive (like the retracted 0x4A8
cyclic counter) can be caught by checking the same (id, pair) on the other
log.
"""
import sys
from collections import defaultdict
from parse_log import parse

# (id, byte_lo) pairs already fully explained by a confirmed/strong-hypothesis
# signal in dbc/toyota_yaris_xp130_reversed.dbc -- skip these to cut noise.
EXPLAINED = {
    (0x0b4, 4), (0x0b4, 5),          # SPEED
    (0x1c4, 0), (0x1c4, 1),          # RPM
    (0x127, 5),                       # GEAR (+CAR_MOVEMENT@4, COUNTER@6, CHECKSUM@7)
    (0x230, 3),                       # BRAKE_PRESSED
    (0x224, 4), (0x224, 5),          # BRAKE_ANALOG
    (0x245, 2),                       # GAS_PEDAL
    (0x611, 5), (0x611, 6), (0x611, 7),  # ODOMETER (20-bit spanning these)
    (0x614, 3),                       # STEERING_LEVERS
    (0x618, 3),                       # TEMP_RAW
    (0x3a0, 7),                       # FUEL_LEVEL
}
# IDs that are fully constant/inactive junk or already fully mapped elsewhere
SKIP_IDS = {0x3b7, 0x399, 0x611}


def wraps(vals):
    nd = len(set(vals))
    if nd < 6:
        return False
    span = max(vals) - min(vals)
    if span == 0:
        return False
    wrap_events = sum(1 for a, b in zip(vals, vals[1:]) if abs(a - b) > span * 0.5)
    return wrap_events >= 3


def classify(vals):
    nd = len(set(vals))
    if nd < 4:
        return None
    if wraps(vals):
        return None
    diffs = [b - a for a, b in zip(vals, vals[1:])]
    nz = [d for d in diffs if d != 0]
    if not nz:
        return None
    changes = len(nz)
    return (nd, changes, min(vals), max(vals))


def main(path):
    frames = parse(path)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append(data)

    results = []
    for cid, datas in by_id.items():
        if cid in SKIP_IDS:
            continue
        dlc = max(len(d) for d in datas)
        pairs = []
        for i in range(dlc - 1):
            pairs.append(('be_adj', i, i + 1))
            pairs.append(('le_adj', i, i + 1))
        for i in range(dlc - 2):
            pairs.append(('be_skip1', i, i + 2))
        for kind, lo, hi in pairs:
            if (cid, lo) in EXPLAINED or (cid, hi) in EXPLAINED:
                continue
            vals = []
            for d in datas:
                if len(d) <= hi:
                    continue
                a, b = d[lo], d[hi]
                if kind == 'be_adj' or kind == 'be_skip1':
                    v = (a << 8) | b
                else:
                    v = (b << 8) | a
                vals.append(v)
            if len(vals) < 20:
                continue
            r = classify(vals)
            if r is None:
                continue
            nd, changes, mn, mx = r
            # SOC-like: bounded span not too huge (rules out raw sensor noise
            # spanning full 16-bit range), moves more than a couple times,
            # and does NOT already look like a checksum/counter (those tend
            # to have nd close to sample count).
            span = mx - mn
            if span > 20000:
                continue
            if nd > 400:
                continue
            if changes < 3:
                continue
            results.append((cid, kind, lo, hi, nd, changes, mn, mx, span))

    results.sort(key=lambda r: r[4])  # fewer distinct values first (more SOC%-bar-like)
    print(f"{'ID':>6} {'kind':>8} {'lo':>3} {'hi':>3} {'ndist':>6} {'chg':>5} {'min':>6} {'max':>6} {'span':>6}")
    for cid, kind, lo, hi, nd, changes, mn, mx, span in results:
        print(f"{cid:#06x} {kind:>8} {lo:3d} {hi:3d} {nd:6d} {changes:5d} {mn:6d} {mx:6d} {span:6d}")


if __name__ == "__main__":
    main(sys.argv[1])
