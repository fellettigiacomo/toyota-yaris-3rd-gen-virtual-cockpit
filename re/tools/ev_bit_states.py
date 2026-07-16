#!/usr/bin/env python3
"""For each candidate EV bit, show P(bit=1) in the four physical states:
(stopped, ICE off), (stopped, ICE on), (moving, ICE off), (moving, ICE on).

An 'ICE running' status bit tracks the ICE columns regardless of motion.
A true 'EV lamp' bit is ON only in (moving, ICE off) — and OFF while
stopped even with the ICE off.
"""
import sys
from collections import defaultdict
from parse_log import parse

CANDIDATES = [
    (0x245, 3, 4),
    (0x49C, 5, 6),
    (0x4AD, 3, 6),
    (0x498, 5, 7),
    (0x49B, 1, 1),
    (0x49C, 3, 6),
    (0x499, 3, 4),
    (0x49A, 0, 6),
    (0x3B6, 5, 2),
    (0x4AD, 6, 2),
]


def main(path):
    frames = parse(path)
    rpm = 0.0
    speed = 0.0
    counts = {c: defaultdict(lambda: [0, 0]) for c in CANDIDATES}  # state -> [n, ones]
    for ts, cid, data in frames:
        if cid == 0x1C4 and len(data) >= 2:
            raw = (data[0] << 8) | data[1]
            if raw >= 0x8000:
                raw -= 0x10000
            rpm = raw * 0.78125
        elif cid == 0x0B4 and len(data) >= 7:
            speed = ((data[5] << 8) | data[6]) * 0.01
        state = ("moving" if speed > 1 else "stopped",
                 "ICEoff" if rpm < 50 else "ICEon")
        for (ccid, bi, bit) in CANDIDATES:
            if cid == ccid and len(data) > bi:
                v = (data[bi] >> bit) & 1
                counts[(ccid, bi, bit)][state][0] += 1
                counts[(ccid, bi, bit)][state][1] += v

    states = [("stopped", "ICEoff"), ("stopped", "ICEon"),
              ("moving", "ICEoff"), ("moving", "ICEon")]
    print(f"\n{path}")
    hdr = "  ".join(f"{s[0]}/{s[1]:>6}" for s in states)
    print(f"{'candidate':>22}  {hdr}")
    for c in CANDIDATES:
        row = []
        for s in states:
            n, ones = counts[c][s]
            row.append(f"{100*ones/n:6.1f}% n={n:<6}" if n else "   --  n=0    ")
        print(f"0x{c[0]:03x} b{c[1]} bit{c[2]:>2}      " + "  ".join(row))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
