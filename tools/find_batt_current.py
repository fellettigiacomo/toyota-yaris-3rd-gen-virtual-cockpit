#!/usr/bin/env python3
"""Search for an HV-battery current/power-like signed 16-bit field.

Signature: sign flips with energy direction —
  discharge (EV drive under power)      -> one sign
  charge (regen braking / ICE charging at standstill) -> opposite sign
Checked on every adjacent byte pair, both logs. A field that is merely
throttle-following (always positive) or speed-following fails this test.
"""
import sys
from parse_log import parse


def s16(hi, lo):
    v = (hi << 8) | lo
    return v - 0x10000 if v >= 0x8000 else v


def main(path):
    frames = parse(path)
    speed = gas = 0.0
    ev = ice = brake = 0
    buckets = {}  # (cid, bi) -> {state: [sum, n, sumabs]}
    for ts, cid, d in frames:
        if cid == 0x0B4 and len(d) >= 7:
            speed = ((d[5] << 8) | d[6]) * 0.01
        elif cid == 0x498 and len(d) > 5:
            ev = (d[5] >> 7) & 1
        elif cid == 0x245 and len(d) > 3:
            ice = (d[3] >> 4) & 1
            gas = d[2] * 0.005
        elif cid == 0x230 and len(d) > 3:
            brake = (d[3] >> 2) & 1
        if cid in (0x0B4, 0x1C4, 0x245):
            continue
        # states of interest only
        if ev and speed > 3 and gas > 0.03 and not brake:
            st = 'discharge'
        elif speed > 5 and brake and ev:
            st = 'regen'
        elif speed < 1 and ice:
            st = 'chargeidle'
        else:
            continue
        for bi in range(len(d) - 1):
            v = s16(d[bi], d[bi + 1])
            key = (cid, bi)
            b = buckets.setdefault(key, {})
            s = b.setdefault(st, [0, 0, 0])
            s[0] += v
            s[1] += 1
            s[2] += abs(v)

    print(f"\n{path}")
    found = 0
    for (cid, bi), b in sorted(buckets.items()):
        if not all(k in b for k in ('discharge', 'regen', 'chargeidle')):
            continue
        md = b['discharge'][0] / b['discharge'][1]
        mr = b['regen'][0] / b['regen'][1]
        mc = b['chargeidle'][0] / b['chargeidle'][1]
        # want: discharge one sign, regen AND chargeidle the opposite sign,
        # with magnitudes well clear of zero
        if md == 0:
            continue
        if (md > 0) != (mr > 0) and (md > 0) != (mc > 0) and \
           abs(mr) > 0.15 * abs(md) and abs(mc) > 0.05 * abs(md) and abs(md) > 20:
            print(f"  id={cid:#05x} bytes[{bi}:{bi+2}] mean discharge={md:+9.1f} "
                  f"regen={mr:+9.1f} charge_idle={mc:+9.1f} "
                  f"(n={b['discharge'][1]}/{b['regen'][1]}/{b['chargeidle'][1]})")
            found += 1
    if not found:
        print("  no field with a clean charge/discharge sign flip found")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
