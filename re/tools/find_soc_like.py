#!/usr/bin/env python3
"""Look for a byte whose value anti-correlates with sustained throttle
(battery drains under acceleration) and/or correlates with braking
(regen charges it), while staying bounded (not full-range noise/checksum)
and not simply wrapping like a counter."""
import sys
from collections import defaultdict
from parse_log import parse
import cantools

EXCLUDE = {
    (0x127, 3), (0x127, 4), (0x127, 5), (0x127, 6), (0x127, 7),
    (0x230, 3), (0x614, 1), (0x614, 3), (0x618, 1), (0x618, 3),
    (0x3a0, 7), (0x4a8, 2), (0x1c4, 0), (0x1c4, 1), (0x0b4, 4), (0x0b4, 5), (0x0b4, 7),
    (0x245, 2), (0x224, 4), (0x224, 5),
}


def wraps(vals, nd):
    # detect if a mod-N counter-like wrap occurs (big jump from near-max to near-min repeatedly)
    if nd < 4:
        return False
    span = max(vals) - min(vals)
    wrap_events = 0
    for a, b in zip(vals, vals[1:]):
        if a - b > span * 0.6 and a > (max(vals) + min(vals)) / 2:
            wrap_events += 1
    return wrap_events >= 2


def main(path):
    frames = parse(path)
    db = cantools.database.load_file('/tmp/opendbc_boggyver/toyota_nodsu_hybrid_pt_generated.dbc')
    gas_msg = db.get_message_by_frame_id(0x245)

    # build a time-aligned "last gas pedal value" sample stream
    last_gas = 0.0
    gas_series = []  # (ts, gas)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        if cid == 0x245:
            last_gas = gas_msg.decode(data, decode_choices=False, allow_truncated=True)['GAS_PEDAL']
        by_id[cid].append((ts, data, last_gas))

    results = []
    for cid, entries in by_id.items():
        dlc = max(len(d) for _, d, _ in entries)
        for bi in range(dlc):
            if (cid, bi) in EXCLUDE:
                continue
            vals = [d[bi] for _, d, _ in entries if len(d) > bi]
            gases = [g for _, d, g in entries if len(d) > bi]
            nd = len(set(vals))
            if nd < 6 or nd > 80:
                continue
            if wraps(vals, nd):
                continue
            # simple correlation between value and gas pedal
            n = len(vals)
            mean_v = sum(vals) / n
            mean_g = sum(gases) / n
            cov = sum((v - mean_v) * (g - mean_g) for v, g in zip(vals, gases)) / n
            var_v = sum((v - mean_v) ** 2 for v in vals) / n
            var_g = sum((g - mean_g) ** 2 for g in gases) / n
            if var_v == 0 or var_g == 0:
                continue
            corr = cov / (var_v ** 0.5 * var_g ** 0.5)
            if abs(corr) >= 0.15:
                results.append((cid, bi, corr, nd, min(vals), max(vals)))

    results.sort(key=lambda r: -abs(r[2]))
    for cid, bi, corr, nd, mn, mx in results[:25]:
        print(f"id={cid:#05x} byte={bi} corr_with_gas={corr:+.3f} distinct={nd} range=[{mn},{mx}]")


if __name__ == "__main__":
    main(sys.argv[1])
