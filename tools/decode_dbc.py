#!/usr/bin/env python3
"""Decode log frames against a DBC and print per-signal time series stats
for every message ID that's present both in the DBC and in the log."""
import sys
import cantools
from collections import defaultdict
from parse_log import parse


def main(dbc_path, log_path):
    db = cantools.database.load_file(dbc_path)
    frames = parse(log_path)
    by_id = defaultdict(list)
    for ts, cid, data in frames:
        by_id[cid].append((ts, data))

    msgs_by_id = {m.frame_id: m for m in db.messages}

    for cid in sorted(by_id):
        if cid not in msgs_by_id:
            continue
        msg = msgs_by_id[cid]
        entries = by_id[cid]
        print(f"\n=== {cid:#05x} {msg.name} n={len(entries)} ===")
        decoded_sigs = defaultdict(list)
        errs = 0
        for ts, data in entries:
            try:
                d = msg.decode(data, decode_choices=False, allow_truncated=True)
                for k, v in d.items():
                    decoded_sigs[k].append(v)
            except Exception:
                errs += 1
        if errs:
            print(f"  ({errs} decode errors out of {len(entries)})")
        for sig, vals in decoded_sigs.items():
            distinct = sorted(set(vals))
            if len(distinct) <= 12:
                print(f"  {sig:25s} distinct={len(distinct):4d} values={distinct}")
            else:
                print(f"  {sig:25s} distinct={len(distinct):4d} min={min(distinct)} max={max(distinct)} first10={vals[:10]}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
