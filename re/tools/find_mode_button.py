#!/usr/bin/env python3
"""Find a byte/bit whose value pulses in bursts matching a known button-press
test pattern: N deliberate bursts of rapid taps, separated by multi-second
gaps, with the byte/bit sitting at one dominant "idle" value the rest of the
time.

Built to find the steering-wheel MODE button (see docs/signal_findings.md,
0x4AC byte[6]) from a capture where the button was tapped 5 times ~0.5s
apart, 3 times, with ~5s gaps between the 3 bursts. Adjust the EXPECT_*
constants below to match a different capture's press pattern (e.g. for
VOL+/-, SRC, SEEK on the same steering wheel cluster).

Usage: find_mode_button.py LOG
"""
import sys
from collections import defaultdict, Counter

from parse_log import parse

MERGE_GAP_S = 1.2      # sub-pulses closer than this merge into one burst
WAKEUP_IGNORE_S = 4.5  # ignore bursts entirely confined to CAN-bus-wakeup noise
EXPECT_GROUPS = 3
EXPECT_BURST_DUR = 2.2
EXPECT_GAP = 5.0
SCORE_THRESHOLD = -20


def merge_runs(runs):
    if not runs:
        return []
    bursts = [[runs[0]]]
    for r in runs[1:]:
        if r[0] - bursts[-1][-1][1] <= MERGE_GAP_S:
            bursts[-1].append(r)
        else:
            bursts.append([r])
    spans = [(g[0][0], g[-1][1]) for g in bursts]
    return [(s, e) for s, e in spans if e >= WAKEUP_IGNORE_S]


def score_spans(spans):
    if not spans:
        return -999
    score = -abs(len(spans) - EXPECT_GROUPS) * 5
    durs = [e - s for s, e in spans]
    score -= sum(abs(d - EXPECT_BURST_DUR) for d in durs) * 0.5
    if len(spans) >= 2:
        gaps = [spans[i + 1][0] - spans[i][1] for i in range(len(spans) - 1)]
        score -= sum(abs(g - EXPECT_GAP) for g in gaps) * 0.3
    return score


def value_runs(vals, idle_val):
    runs, cur_start, cur_end = [], None, None
    for ts, v in vals:
        if v != idle_val:
            if cur_start is None:
                cur_start = ts
            cur_end = ts
        else:
            if cur_start is not None:
                runs.append((cur_start, cur_end))
                cur_start = None
    if cur_start is not None:
        runs.append((cur_start, cur_end))
    return runs


def bit_runs(bits, minority):
    runs, cur_start, cur_end = [], None, None
    for ts, b in bits:
        if b == minority:
            if cur_start is None:
                cur_start = ts
            cur_end = ts
        else:
            if cur_start is not None:
                runs.append((cur_start, cur_end))
                cur_start = None
    if cur_start is not None:
        runs.append((cur_start, cur_end))
    return runs


def analyze(path):
    frames = parse(path)
    t0 = frames[0][0]
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append((ts - t0, data))

    results = []
    for cid, entries in by_id.items():
        if len(entries) < 10:
            continue
        dlc = max(len(d) for _, d in entries)
        for bi in range(dlc):
            byte_series = [(ts, d[bi]) for ts, d in entries if len(d) > bi]
            if len(byte_series) < 10:
                continue

            counts = Counter(v for _, v in byte_series)
            if len(counts) >= 2:
                idle_val, idle_count = counts.most_common(1)[0]
                if idle_count / len(byte_series) >= 0.4:
                    spans = merge_runs(value_runs(byte_series, idle_val))
                    sc = score_spans(spans)
                    if sc > SCORE_THRESHOLD:
                        results.append(("byte", cid, bi, None, idle_val, spans, sc))

            for bit in range(8):
                bits = [(ts, (v >> bit) & 1) for ts, v in byte_series]
                ones = sum(b for _, b in bits)
                if ones == 0 or ones == len(bits):
                    continue
                dom = 1 if ones * 2 > len(bits) else 0
                minority = 1 - dom
                minority_frac = (ones if minority == 1 else len(bits) - ones) / len(bits)
                if minority_frac > 0.45:
                    continue
                spans = merge_runs(bit_runs(bits, minority))
                sc = score_spans(spans)
                if sc > SCORE_THRESHOLD:
                    results.append(("bit", cid, bi, bit, dom, spans, sc))

    results.sort(key=lambda r: -r[-1])
    print(f"{len(results)} candidates (score > {SCORE_THRESHOLD})\n")
    for kind, cid, bi, bit, idleinfo, spans, sc in results[:30]:
        bitstr = f" bit={bit}" if bit is not None else ""
        durs = [round(e - s, 2) for s, e in spans]
        gaps = [round(spans[i + 1][0] - spans[i][1], 2) for i in range(len(spans) - 1)] if len(spans) > 1 else []
        print(f"score={sc:6.1f} [{kind}] id={cid:#05x} byte={bi}{bitstr} idle=0x{idleinfo:02x} "
              f"n_bursts={len(spans)}")
        print(f"    spans={[(round(s,2), round(e,2)) for s,e in spans]} durs={durs} gaps={gaps}")


if __name__ == "__main__":
    analyze(sys.argv[1])
