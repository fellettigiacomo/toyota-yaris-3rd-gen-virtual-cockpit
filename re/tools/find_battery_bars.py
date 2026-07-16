#!/usr/bin/env python3
"""Look for a coarse 'battery bars' style signal: enum with 2-8 distinct
values, excluding (id, byte) pairs already fully explained by known
messages. Prints every remaining enum candidate for manual review."""
import sys
from collections import defaultdict
from parse_log import parse

# (id, byte_index) pairs already fully explained -- skip these
KNOWN = {
    (0x127, 5),  # GEAR nibble (+ CAR_MOVEMENT in byte4, counter/checksum in 6/7)
    (0x230, 3),  # BRAKE_PRESSED
    (0x614, 5),  # TURN_SIGNALS / HAZARD_LIGHT
    (0x620, 7),  # SEATS_DOORS bits packed near end
    (0x622, ),   # LIGHT_STALK -- handled by id below
    (0x399,),    # PCM_CRUISE_SM
    (0x3b7,),    # ESP_CONTROL
    (0x224, 4), (0x224, 5),  # brake analog
    (0x245, 2),  # gas pedal
    (0x1c4, 0), (0x1c4, 1),  # RPM
    (0x0b4, 4), (0x0b4, 5), (0x0b4, 7),  # SPEED/ENCODER/CHECKSUM
    (0x611,),  # odometer/units
}
KNOWN_IDS_WHOLE = {0x622, 0x399, 0x3b7, 0x611, 0x024, 0x025, 0x260}


def classify(values):
    distinct = sorted(set(values))
    nd = len(distinct)
    if nd == 1:
        return 'const', nd, distinct
    diffs = [b - a for a, b in zip(values, values[1:])]
    nz = [d for d in diffs if d != 0]
    n_pos = sum(1 for d in nz if d > 0)
    n_neg = sum(1 for d in nz if d < 0)
    if nz and (n_neg == 0 or n_pos == 0):
        return 'monotonic', nd, distinct
    return 'enum' if nd <= 8 else 'oscillating', nd, distinct


def main(path):
    frames = parse(path)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append((ts, data))

    for cid in sorted(by_id):
        if cid in KNOWN_IDS_WHOLE:
            continue
        entries = by_id[cid]
        dlc = max(len(d) for _, d in entries)
        for bi in range(dlc):
            if (cid, bi) in KNOWN or (cid,) in KNOWN:
                continue
            vals = [d[bi] for _, d in entries if len(d) > bi]
            tag, nd, distinct = classify(vals)
            if tag == 'enum' and 2 <= nd <= 8:
                # print with first-seen timestamps for each distinct value
                first_seen = {}
                for ts, d in entries:
                    if len(d) > bi:
                        v = d[bi]
                        if v not in first_seen:
                            first_seen[v] = ts
                t0 = entries[0][0]
                fs = {v: round(t - t0, 1) for v, t in first_seen.items()}
                print(f"id={cid:#05x} byte={bi} distinct={distinct} first_seen_at_s={fs}")


if __name__ == "__main__":
    main(sys.argv[1])
