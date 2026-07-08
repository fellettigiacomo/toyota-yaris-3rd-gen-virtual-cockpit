#!/usr/bin/env python3
"""SOC detector based on *derivative* physics instead of instantaneous
correlation (a real SOC integrates power, so it barely correlates with
the gas pedal at zero lag — the earlier find_soc_like.py scan could miss it).

For every (id, byte) that varies slowly within a bounded band, look at each
value *change* and score it against the expected battery physics:
  - decrement while driving electric (EV bit 0x498 b5.7 = 1)  -> discharge OK
  - increment while ICE running (0x245 b3.4 = 1) or braking    -> charge OK
A real SOC should have most steps agree; a counter/temp/fuel will not.

Also prints the value timeline so the shape can be eyeballed.
"""
import sys
from collections import defaultdict
from parse_log import parse


def main(path, min_distinct=3, max_distinct=40, min_dwell=2.0):
    frames = parse(path)
    ev = 0
    ice = 0
    brake = 0
    by_key = defaultdict(list)  # (cid,byte) -> list of (ts, val, ev, ice, brake)
    for ts, cid, data in frames:
        if cid == 0x498 and len(data) > 5:
            ev = (data[5] >> 7) & 1
        elif cid == 0x245 and len(data) > 3:
            ice = (data[3] >> 4) & 1
        elif cid == 0x230 and len(data) > 3:
            brake = (data[3] >> 2) & 1
        for bi in range(len(data)):
            by_key[(cid, bi)].append((ts, data[bi], ev, ice, brake))

    results = []
    for (cid, bi), entries in by_key.items():
        if cid in (0x498, 0x245, 0x230, 0x1C4, 0x0B4):
            continue
        vals = [v for _, v, _, _, _ in entries]
        distinct = set(vals)
        if not (min_distinct <= len(distinct) <= max_distinct):
            continue
        if max(vals) - min(vals) > 60:  # SOC display band is narrow
            continue
        # collect changes with dwell times
        changes = []  # (ts, delta, ev, ice, brake)
        last_v = entries[0][1]
        last_t = entries[0][0]
        dwells = []
        for ts, v, e, i, b in entries[1:]:
            if v != last_v:
                dwells.append(ts - last_t)
                changes.append((ts, v - last_v, e, i, b))
                last_v = v
                last_t = ts
        if len(changes) < 4 or len(changes) > 200:
            continue
        med_dwell = sorted(dwells)[len(dwells) // 2]
        if med_dwell < min_dwell:
            continue
        # steps must be small (SOC moves 1-2 display units at a time)
        if any(abs(d) > 8 for _, d, _, _, _ in changes):
            continue
        good = 0
        for _, d, e, i, b in changes:
            if d < 0 and e and not i:
                good += 1
            elif d > 0 and (i or b):
                good += 1
        score = good / len(changes)
        n_up = sum(1 for _, d, _, _, _ in changes if d > 0)
        n_dn = len(changes) - n_up
        # both directions must occur (SOC wanders up AND down)
        if n_up == 0 or n_dn == 0:
            continue
        results.append((score, cid, bi, len(changes), n_up, n_dn,
                        min(vals), max(vals), med_dwell))

    results.sort(key=lambda r: -r[0])
    print(f"\n{path}")
    for score, cid, bi, nc, nu, nd, mn, mx, dw in results[:20]:
        print(f"  id={cid:#05x} byte={bi} physics_score={score:.2f} "
              f"changes={nc} (+{nu}/-{nd}) range=[{mn},{mx}] med_dwell={dw:.1f}s")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
