#!/usr/bin/env python3
"""For every CAN ID present in both logs, compare per-byte distinct-value
counts. Flag bytes that were constant (or low-cardinality) in log A but
became much more variable in log B -- a sign that some ECU field got
"woken up" by traffic present only in B (e.g. the aftermarket box's
queries)."""
import sys
from collections import defaultdict
from parse_log import parse


def byte_distincts(frames, target_ids=None):
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        if target_ids is None or cid in target_ids:
            by_id[cid].append(data)
    result = {}
    for cid, datas in by_id.items():
        dlc = max(len(d) for d in datas)
        result[cid] = []
        for bi in range(dlc):
            vals = [d[bi] for d in datas if len(d) > bi]
            result[cid].append(len(set(vals)))
    return result


def main(path_a, path_b):
    fa = parse(path_a)
    fb = parse(path_b)
    ids_common = set(c for _, c, _ in fa) & set(c for _, c, _ in fb)
    da = byte_distincts(fa, ids_common)
    db = byte_distincts(fb, ids_common)
    for cid in sorted(ids_common):
        la, lb = da.get(cid, []), db.get(cid, [])
        for bi in range(min(len(la), len(lb))):
            # flag: was near-constant in A (<=2 distinct), now much richer in B
            if la[bi] <= 2 and lb[bi] >= 8:
                print(f"id={cid:#05x} byte={bi}: distinct_A={la[bi]} distinct_B={lb[bi]}  <-- became active")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
