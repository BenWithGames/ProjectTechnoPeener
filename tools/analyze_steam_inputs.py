"""Compare delivered diagnostic inputs by observed outer counter, not sample index."""
import argparse
import csv
import json


def read(path):
    with open(path, newline="") as source:
        rows = [{key: int(value) for key, value in row.items()}
                for row in csv.DictReader(source)]
    if len(rows) not in (120, 1800) or any(row["sample"] != i for i, row in enumerate(rows)):
        raise ValueError("Expected 120 or 1800 ordered samples")
    counters = [row["word0"] for row in rows]
    if len(set(counters)) != len(counters):
        raise ValueError("Repeated frame counter")
    return rows


def compare(local, remote):
    lookup = {row["word0"]: row for row in remote}
    pairs = [(row, lookup[row["word0"]]) for row in local if row["word0"] in lookup]
    return {
        "scope": "captured_input_comparison_only",
        "local_start": local[0]["word0"],
        "remote_start": remote[0]["word0"],
        "overlapping_counters": len(pairs),
        "player_words_equal": sum(all(a[f"word{w}"] == b[f"word{w}"]
                                      for w in range(1, 5)) for a, b in pairs),
        "first_slot_nonzero_samples": [sum(bool(r["word1"] or r["word2"]) for r in rows)
                                       for rows in (local, remote)],
        "second_slot_nonzero_samples": [sum(bool(r["word3"] or r["word4"]) for r in rows)
                                        for rows in (local, remote)],
        "counter_discontinuities": [sum(b["word0"] != (a["word0"] + 1) % (2**32)
                                        for a, b in zip(rows, rows[1:]))
                                    for rows in (local, remote)],
        "player_identity_verified": False,
        "general_frame_alignment_verified": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("local")
    parser.add_argument("remote")
    parser.add_argument("--timing")
    parser.add_argument("--qpc-frequency", type=int)
    args = parser.parse_args()
    local, remote = read(args.local), read(args.remote)
    result = compare(local, remote)
    if args.timing:
        if not args.qpc_frequency or args.qpc_frequency <= 0:
            parser.error("--timing requires the local report's --qpc-frequency")
        with open(args.timing, newline="") as source:
            timings = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(source)]
        if len(timings) != len(local):
            raise ValueError("Timing length mismatch")
        by_counter = {r["word0"]: r for r in local}
        dispatch, arrival = [], []
        for i, timing in enumerate(timings):
            if timing["sample"] != i or timing["local_outer"] != local[i]["word0"] or timing["remote_outer"] != remote[i]["word0"]:
                raise ValueError("Timing records do not match captures")
            stamp = lambda r: (r["word12"] << 32) | r["word11"]
            if timing["first_send_qpc"]:
                dispatch.append((timing["first_send_qpc"] - stamp(local[i])) * 1000 / args.qpc_frequency)
            reference = by_counter.get(timing["remote_outer"])
            if reference and timing["arrival_qpc"]:
                arrival.append((timing["arrival_qpc"] - stamp(reference)) * 1000 / args.qpc_frequency)
        def stats(values):
            ordered = sorted(values)
            return None if not ordered else {"count": len(values), "min_ms": ordered[0], "median_ms": ordered[len(ordered)//2], "p95_ms": ordered[int((len(ordered)-1)*.95)], "max_ms": ordered[-1]}
        result["capture_to_first_send"] = stats(dispatch)
        result["arrival_relative_to_local_same_counter"] = stats(arrival)
        result["timing_is_network_latency"] = False
    print(json.dumps(result, indent=2))
