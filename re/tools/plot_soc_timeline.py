#!/usr/bin/env python3
"""Print an ASCII timeline of a candidate SOC byte with vehicle context
(speed / RPM / EV bit / brake), sampled once per second, to eyeball the
physics: SOC must fall during EV stretches and recover during ICE-on
charge or regen braking, without wrapping.

Usage: plot_soc_timeline.py LOG CANID BYTE [--scale 0.5]
"""
import sys
from parse_log import parse


def main(path, cid_want, bi, scale=0.5):
    frames = parse(path)
    rpm = speed = 0.0
    ev = brake = 0
    val = None
    t0 = frames[0][0]
    rows = []
    next_t = 0.0
    for ts, cid, data in frames:
        if cid == 0x1C4 and len(data) >= 2:
            raw = (data[0] << 8) | data[1]
            if raw >= 0x8000:
                raw -= 0x10000
            rpm = raw * 0.78125
        elif cid == 0x0B4 and len(data) >= 7:
            speed = ((data[5] << 8) | data[6]) * 0.01
        elif cid == 0x498 and len(data) > 5:
            ev = (data[5] >> 7) & 1
        elif cid == 0x230 and len(data) > 3:
            brake = (data[3] >> 2) & 1
        elif cid == cid_want and len(data) > bi:
            val = data[bi]
        t = ts - t0
        if t >= next_t and val is not None:
            rows.append((t, val, speed, rpm, ev, brake))
            next_t += 2.0

    lo = min(r[1] for r in rows)
    hi = max(r[1] for r in rows)
    print(f"{path}  id={cid_want:#05x} byte={bi}  raw range [{lo},{hi}] "
          f"= [{lo*scale:.1f},{hi*scale:.1f}] @ scale {scale}")
    print(f"{'t(s)':>6} {'raw':>4} {'x0.5':>6} {'kph':>6} {'rpm':>6} EV BRK  bar")
    for t, v, sp, r, e, b in rows:
        bar = "#" * (v - lo + 1)
        print(f"{t:6.0f} {v:4d} {v*scale:6.1f} {sp:6.1f} {r:6.0f}  {e}   {b}   {bar}")


if __name__ == "__main__":
    a = sys.argv
    main(a[1], int(a[2], 16), int(a[3]), float(a[4]) if len(a) > 4 else 0.5)
