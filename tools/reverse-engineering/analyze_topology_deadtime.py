#!/usr/bin/env python3
"""Extract MCL02M topology-heartbeat transitions from analyze_sigrok_i2c JSON."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path


TOPOLOGY_NAMES = {0x00: "STOP", 0x80: "NO_PAN", 0xA1: "A", 0xC1: "B", 0xE1: "AB"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()

    decoded = json.loads(args.report.read_text(encoding="utf-8"))["best"]
    writes: list[dict] = []
    for frame in decoded["frames"]:
        data = frame["bytes"]
        if (frame["direction"] != "write" or len(data) != 4 or data[0] != 0x54):
            continue
        register, value, checksum = data[1:]
        if register not in (0x0D, 0x00, 0x0C) or ((register + value) & 0xFF) != checksum:
            continue
        writes.append({"t_s": frame["start_s"], "register": register, "value": value})

    cycles: list[dict] = []
    for index, item in enumerate(writes):
        if item["register"] != 0x0D:
            continue
        following = writes[index + 1:index + 3]
        values = {entry["register"]: entry for entry in following}
        if 0x00 not in values or 0x0C not in values:
            continue
        if values[0x0C]["t_s"] - item["t_s"] > 0.1:
            continue
        topology = item["value"]
        cycles.append({
            "index": len(cycles),
            "t_s": item["t_s"],
            "w0d": topology,
            "topology": TOPOLOGY_NAMES.get(topology, f"0x{topology:02X}"),
            "w00": values[0x00]["value"],
            "gear": values[0x0C]["value"],
            "w00_t_s": values[0x00]["t_s"],
            "gear_t_s": values[0x0C]["t_s"],
        })

    transitions: list[dict] = []
    previous = cycles[0] if cycles else None
    for cycle in cycles[1:]:
        if previous is not None and cycle["w0d"] != previous["w0d"]:
            intermediate = cycles[previous["index"] + 1:cycle["index"]]
            transitions.append({
                "t_s": cycle["t_s"],
                "from": previous["topology"],
                "to": cycle["topology"],
                "from_w0d": f"0x{previous['w0d']:02X}",
                "to_w0d": f"0x{cycle['w0d']:02X}",
                "previous_gear": previous["gear"],
                "new_gear": cycle["gear"],
                "w00": cycle["w00"],
                "w0d_to_w00_ms": round((cycle["w00_t_s"] - cycle["t_s"]) * 1000, 3),
                "w0d_to_w0c_ms": round((cycle["gear_t_s"] - cycle["t_s"]) * 1000, 3),
                "elapsed_from_previous_w0d_s": round(cycle["t_s"] - previous["t_s"], 6),
                "zero_command_between": any(
                    entry["w0d"] == 0 or entry["w00"] == 0 or entry["gear"] == 0
                    for entry in intermediate
                ),
            })
        previous = cycle

    active_transitions = [
        event for event in transitions
        if event["from"] not in ("STOP", "NO_PAN") and event["to"] not in ("STOP", "NO_PAN")
    ]
    rapid_intervals = [
        round(active_transitions[index]["t_s"] - active_transitions[index - 1]["t_s"], 6)
        for index in range(1, len(active_transitions))
        if active_transitions[index]["t_s"] - active_transitions[index - 1]["t_s"] < 3.2
    ]
    periods = [cycles[index]["t_s"] - cycles[index - 1]["t_s"] for index in range(1, len(cycles))]
    result = {
        "capture": decoded["source"],
        "duration_s": decoded["duration_s"],
        "sample_rate_hz": decoded["sample_rate_hz"],
        "decoded_frames": len(decoded["frames"]),
        "control_write_count": len(writes),
        "heartbeat_cycles": len(cycles),
        "heartbeat_period_median_s": statistics.median(periods),
        "heartbeat_period_min_s": min(periods),
        "heartbeat_period_max_s": max(periods),
        "all_topology_transitions": len(transitions),
        "active_topology_transitions": len(active_transitions),
        "active_transitions_with_zero_command": sum(event["zero_command_between"] for event in active_transitions),
        "rapid_active_transition_intervals_s": rapid_intervals,
        "transitions": transitions,
    }
    args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    with args.csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(transitions[0].keys()))
        writer.writeheader()
        writer.writerows(transitions)

    print(json.dumps({key: value for key, value in result.items() if key != "transitions"},
                     ensure_ascii=False, indent=2))
    print("TRANSITIONS")
    for event in transitions:
        print(f"{event['t_s']:10.6f}s {event['from']:>4}->{event['to']:<4} "
              f"gear {event['previous_gear']:02d}->{event['new_gear']:02d} "
              f"W00={event['w00']} zero_between={event['zero_command_between']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
