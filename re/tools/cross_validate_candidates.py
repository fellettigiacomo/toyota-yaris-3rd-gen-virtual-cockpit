#!/usr/bin/env python3
"""Search every (id, byte-pair) 16-bit combination (adjacent BE/LE + skip-1)
across TWO logs at once, keep only candidates that show up in BOTH with a
non-wrapping, multi-valued, overlapping-range signal, and report correlation
with GAS_PEDAL (0x245 byte2) and BRAKE_PRESSED (0x230 bit26) for each.

This exists because single-log scans produced false positives before (the
0x4A8 byte2 top-3-bit "battery bars" candidate turned out to be a cyclic
counter when checked against the second log) -- cross-log + range-overlap is
now the standard bar for taking a candidate seriously enough to inspect by
hand.

Usage: python3 cross_validate_candidates.py <logA> <logB>
"""
import sys
from collections import defaultdict
from parse_log import parse

EXPLAINED = {
    (0x0b4, 4), (0x0b4, 5), (0x1c4, 0), (0x1c4, 1), (0x127, 5), (0x230, 3),
    (0x224, 4), (0x224, 5), (0x245, 2), (0x611, 5), (0x611, 6), (0x611, 7),
    (0x614, 3), (0x618, 3), (0x3a0, 7), (0x3b9, 0),
}
SKIP_IDS = {0x3b7, 0x399, 0x611}
KNOWN_GAS_DUP = {0x361, 0x49b}


def wraps(vals):
    nd = len(set(vals))
    if nd < 6:
        return False
    span = max(vals) - min(vals)
    if span == 0:
        return False
    return sum(1 for a, b in zip(vals, vals[1:]) if abs(a - b) > span * 0.5) >= 3


def brake_bit(d):
    # empirically verified against docs' 5731-pressed-sample count: bit2 of byte3
    return (d[3] >> 2) & 1 if len(d) > 3 else 0


def corr(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    cov = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / n
    vx = sum((x - mx) ** 2 for x in xs) / n
    vy = sum((y - my) ** 2 for y in ys) / n
    return cov / (vx ** 0.5 * vy ** 0.5) if vx > 0 and vy > 0 else 0


def gather(path):
    frames = parse(path)
    by_id = defaultdict(list)
    last_gas = last_brake = 0
    for ts, cid, data in frames:
        if cid == 0x245 and len(data) > 2:
            last_gas = data[2]
        if cid == 0x230:
            last_brake = brake_bit(data)
        by_id[cid].append((data, last_gas, last_brake))
    return by_id


def candidates(by_id):
    out = {}
    for cid, entries in by_id.items():
        if cid in SKIP_IDS or cid in KNOWN_GAS_DUP:
            continue
        dlc = max(len(d) for d, _, _ in entries)
        pairs = []
        for i in range(dlc - 1):
            pairs.append(('be_adj', i, i + 1))
            pairs.append(('le_adj', i, i + 1))
        for i in range(dlc - 2):
            pairs.append(('be_skip1', i, i + 2))
        for kind, lo, hi in pairs:
            if (cid, lo) in EXPLAINED or (cid, hi) in EXPLAINED:
                continue
            vals, gases, brakes = [], [], []
            for d, g, b in entries:
                if len(d) <= hi:
                    continue
                a2, b2 = d[lo], d[hi]
                v = (a2 << 8) | b2 if kind != 'le_adj' else (b2 << 8) | a2
                vals.append(v)
                gases.append(g)
                brakes.append(b)
            if len(vals) < 20:
                continue
            nd = len(set(vals))
            if nd < 4 or wraps(vals):
                continue
            if not any(y - x != 0 for x, y in zip(vals, vals[1:])):
                continue
            span = max(vals) - min(vals)
            if span > 20000 or nd > 400:
                continue
            out[(cid, kind, lo, hi)] = dict(
                nd=nd, mn=min(vals), mx=max(vals),
                cg=corr(vals, gases), cb=corr(vals, brakes), n=len(vals))
    return out


def main(path_a, path_b):
    candA = candidates(gather(path_a))
    candB = candidates(gather(path_b))
    common = set(candA) & set(candB)
    rows = []
    for key in common:
        a, b = candA[key], candB[key]
        overlap = not (a['mx'] < b['mn'] or b['mx'] < a['mn'])
        if not overlap:
            continue
        rows.append((key, a, b))
    rows.sort(key=lambda r: -(abs(r[1]['cb']) + abs(r[2]['cb'])))
    print(f"{'ID':>6} {'kind':>8} {'lo':>3} {'hi':>3} | {'ndA':>4} {'ndB':>4} "
          f"{'cgA':>7} {'cgB':>7} {'cbA':>7} {'cbB':>7} {'rangeA':>14} {'rangeB':>14}")
    for key, a, b in rows:
        cid, kind, lo, hi = key
        print(f"{cid:#06x} {kind:>8} {lo:3d} {hi:3d} | {a['nd']:4d} {b['nd']:4d} "
              f"{a['cg']:+7.3f} {b['cg']:+7.3f} {a['cb']:+7.3f} {b['cb']:+7.3f} "
              f"[{a['mn']:5d},{a['mx']:5d}] [{b['mn']:5d},{b['mx']:5d}]")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
