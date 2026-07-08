#!/usr/bin/env python3
"""Scan every bit of every ID for agreement with the EV-mode proxy
(RPM < 50 AND SPEED > 1 kph). A real 'EV lamp' bit driven to the dash
should match the proxy state with high agreement and a similar number
of transitions, on BOTH logs independently.

Usage: find_ev_bit.py LOG [LOG2 ...]
"""
import sys
from collections import defaultdict
from parse_log import parse


def build_state_series(frames):
    """Return list of (ts, cid, data, ev_proxy, moving) with proxy sampled
    from the latest RPM/SPEED values."""
    rpm = 0.0
    speed = 0.0
    out = []
    for ts, cid, data in frames:
        if cid == 0x1C4 and len(data) >= 2:
            raw = (data[0] << 8) | data[1]
            if raw >= 0x8000:
                raw -= 0x10000
            rpm = raw * 0.78125
        elif cid == 0x0B4 and len(data) >= 7:
            speed = ((data[5] << 8) | data[6]) * 0.01
        moving = speed > 1.0
        ev = moving and rpm < 50
        out.append((ts, cid, data, ev, moving))
    return out


def transitions(series):
    n = 0
    for a, b in zip(series, series[1:]):
        if a != b:
            n += 1
    return n


def scan(path):
    frames = parse(path)
    seq = build_state_series(frames)
    dur = seq[-1][0] - seq[0][0]
    # proxy stats
    ev_series = [ev for _, _, _, ev, mv in seq if mv]
    print(f"{path}: {dur:.0f}s, proxy true {100*sum(ev_series)/max(1,len(ev_series)):.1f}% of moving samples")

    by_id = defaultdict(list)
    for ts, cid, data, ev, mv in seq:
        by_id[cid].append((ts, data, ev, mv))

    results = []
    for cid, entries in by_id.items():
        if cid in (0x1C4, 0x0B4):
            continue  # inputs of the proxy itself
        dlc = max(len(d) for _, d, _, _ in entries)
        # only consider frames while moving (proxy only meaningful then)
        mv_entries = [(d, ev) for _, d, ev, mv in entries if mv]
        if len(mv_entries) < 200:
            continue
        for bi in range(dlc):
            for bit in range(8):
                bits = []
                evs = []
                for d, ev in mv_entries:
                    if len(d) <= bi:
                        continue
                    bits.append((d[bi] >> bit) & 1)
                    evs.append(ev)
                if not bits:
                    continue
                ones = sum(bits)
                if ones == 0 or ones == len(bits):
                    continue  # constant while moving
                agree = sum(1 for b, e in zip(bits, evs) if b == e) / len(bits)
                agree_inv = 1 - agree
                best = max(agree, agree_inv)
                if best >= 0.85:
                    nt = transitions(bits)
                    nt_ev = transitions(evs)
                    pol = "+" if agree >= agree_inv else "-"
                    results.append((best, cid, bi, bit, pol, nt, nt_ev))

    results.sort(key=lambda r: -r[0])
    for best, cid, bi, bit, pol, nt, nt_ev in results[:30]:
        print(f"  id={cid:#05x} byte={bi} bit={bit} pol={pol} agree={best:.3f} "
              f"bit_transitions={nt} proxy_transitions={nt_ev}")
    if not results:
        print("  no bit with >=85% agreement found")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        scan(p)
