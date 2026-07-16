#!/usr/bin/env python3
"""Cross-validated button/event hunt: find signals that pulse during a
dedicated button-press capture AND stay quiet in reference logs where the
button was never touched.

Built after 0x4AC byte[6] -- found by find_mode_button.py on the capture's
shape alone -- turned out to be a free-running 4.5s-on/4.5s-off oscillator
present identically in every drive log, which cycled the cockpit screens by
itself every 9s on the road. A 31s capture of a 9s square wave shows ~3
active windows of ~4.5s separated by ~4.5s: exactly the burst shape the
single-log scan was told to find. The cross-log check this script adds (the
same standard that had already unmasked 0x4A8 as a false SOC) rejects it
instantly, and is mandatory for any future button search.

Three complementary scans, covering every event-signaling style seen on
this bus:
  1. bit deviations: a bit that leaves its dominant idle value only in
     activity clusters during the capture;
  2. value changes: a byte whose value changes in clusters (event counters,
     enums) during the capture;
  3. transmission timing: an ID that sends early/extra frames (event-
     triggered or accelerated transmission) during the capture.
All three require the same line to be (near-)silent in every reference log.

Usage: find_button_crossval.py CAPTURE_LOG QUIET_REF_LOG [QUIET_REF_LOG...]

Result on mode_button_capture_20260714.log vs session_0044/0047: zero
survivors in all three scans -- the steering-wheel MODE button does not
transit on this CAN bus (it is almost certainly on the analog resistive-
ladder steering-switch wires of the 28-pin radio connector instead).
"""
import statistics
import sys
from collections import Counter, defaultdict

from parse_log import parse

MERGE_GAP_S = 1.2          # sub-pulses closer than this merge into one cluster
MAX_CLUSTERS = 15          # more activity clusters than this = not a deliberate test pattern
MAX_CLUSTER_SPAN_S = 6.0   # a single press/burst can't plausibly last longer
MAX_CAPTURE_DEV = 0.6      # line must be mostly-idle even in the capture
MAX_REF_DEV = 0.005        # fraction of deviating frames tolerated in a quiet ref log
MAX_REF_CHANGES_PER_MIN = 1.0
EARLY_FRAME_FACTOR = 0.6   # gap < 60% of the ID's median period = early/extra frame


def clusters(times):
    if not times:
        return []
    groups = [[times[0]]]
    for t in times[1:]:
        if t - groups[-1][-1] <= MERGE_GAP_S:
            groups[-1].append(t)
        else:
            groups.append([t])
    return [(g[0], g[-1], len(g)) for g in groups]


def plausible(cl):
    return (1 <= len(cl) <= MAX_CLUSTERS
            and max(e - s for s, e, _ in cl) <= MAX_CLUSTER_SPAN_S)


def fmt_clusters(cl):
    return ", ".join(f"[{s:.1f}-{e:.1f}]({n})" for s, e, n in cl[:8])


def bit_series(frames):
    t0 = frames[0][0]
    out = defaultdict(list)
    for ts, cid, data in frames:
        for bi, b in enumerate(data):
            for bit in range(8):
                out[(cid, bi, bit)].append((ts - t0, (b >> bit) & 1))
    return out


def change_times(frames):
    t0 = frames[0][0]
    last = {}
    out = defaultdict(list)
    for ts, cid, data in frames:
        for bi, b in enumerate(data):
            k = (cid, bi)
            if k in last and last[k] != b:
                out[k].append(ts - t0)
            last[k] = b
    return out


def scan_bits(cap_frames, ref_frames_list):
    cap = bit_series(cap_frames)
    refs = [bit_series(f) for f in ref_frames_list]
    hits = []
    for key, series in cap.items():
        idle = Counter(v for _, v in series).most_common(1)[0][0]
        dev = [t for t, v in series if v != idle]
        if not dev or len(dev) / len(series) > MAX_CAPTURE_DEV:
            continue
        cl = clusters(dev)
        if not plausible(cl):
            continue
        if any(key in r and
               sum(1 for _, v in r[key] if v != idle) / len(r[key]) > MAX_REF_DEV
               for r in refs):
            continue
        cid, byte, bit = key
        hits.append(f"0x{cid:03X} byte[{byte}] bit{bit} idle={idle}: {fmt_clusters(cl)}")
    return hits


def scan_value_changes(cap_frames, ref_frames_list):
    cap = change_times(cap_frames)
    refs = [change_times(f) for f in ref_frames_list]
    ref_durs = [f[-1][0] - f[0][0] for f in ref_frames_list]
    hits = []
    for key, ch in cap.items():
        cl = clusters(ch)
        if not plausible(cl):
            continue
        if any(key in r and len(r[key]) / (dur / 60.0) > MAX_REF_CHANGES_PER_MIN
               for r, dur in zip(refs, ref_durs)):
            continue
        cid, byte = key
        hits.append(f"0x{cid:03X} byte[{byte}]: {len(ch)} value changes: {fmt_clusters(cl)}")
    return hits


def scan_early_frames(cap_frames):
    per_id = defaultdict(list)
    t0 = cap_frames[0][0]
    for ts, cid, _ in cap_frames:
        per_id[cid].append(ts - t0)
    hits = []
    for cid, ts in per_id.items():
        if len(ts) < 10:
            continue
        gaps = [b - a for a, b in zip(ts, ts[1:])]
        med = statistics.median(gaps)
        if med <= 0.02:  # already near-continuous, "early" is meaningless
            continue
        early = [ts[i + 1] for i, g in enumerate(gaps) if g < EARLY_FRAME_FACTOR * med]
        cl = clusters(early)
        if early and plausible(cl):
            hits.append(f"0x{cid:03X}: {len(early)} early frames (median period "
                        f"{med * 1000:.0f}ms): {fmt_clusters(cl)}")
    return hits


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cap_frames = parse(sys.argv[1])
    ref_frames_list = [parse(p) for p in sys.argv[2:]]

    for title, hits in [
        ("bit deviations (clustered in capture, silent in refs)",
         scan_bits(cap_frames, ref_frames_list)),
        ("byte value changes (clustered in capture, silent in refs)",
         scan_value_changes(cap_frames, ref_frames_list)),
        ("event-triggered / early frames in capture",
         scan_early_frames(cap_frames)),
    ]:
        print(f"== {title}: {len(hits)} candidate(s)")
        for h in hits:
            print(f"   {h}")


if __name__ == "__main__":
    main()
