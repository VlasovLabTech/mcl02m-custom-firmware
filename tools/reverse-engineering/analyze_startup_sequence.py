#!/usr/bin/env python3
"""Analyze MCL02M stock-firmware start/stop sequences from decoded I2C JSON.

The input is produced by ``analyze_sigrok_i2c.py``.  This script is passive:
it only correlates the eight feedback-register reads with the following
W0D/W00/W0C heartbeat command and reports command/feedback transitions.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path


READ_REGISTERS = tuple(range(0x20, 0x28))
TOPOLOGY_NAMES = {0x00: "STOP", 0x80: "NO_PAN", 0xA1: "A", 0xC1: "B", 0xE1: "AB"}


def valid_checksum(register: int, value: int, checksum: int) -> bool:
    return ((register + value) & 0xFF) == checksum


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()

    decoded = json.loads(args.report.read_text(encoding="utf-8"))["best"]
    frames = decoded["frames"]

    reads: list[dict] = []
    writes: list[dict] = []
    pending_register: tuple[int, float] | None = None
    checksum_errors = 0

    for frame in frames:
        data = frame["bytes"]
        direction = frame["direction"]
        t_s = frame["start_s"]

        if direction == "write" and len(data) == 2 and data[0] == 0x54:
            pending_register = (data[1], t_s)
            continue

        if direction == "read" and len(data) == 3 and data[0] == 0x55 and pending_register:
            register, select_t_s = pending_register
            value, checksum = data[1:]
            pending_register = None
            if not valid_checksum(register, value, checksum):
                checksum_errors += 1
                continue
            reads.append({"t_s": t_s, "register": register, "value": value})
            continue

        if direction == "write" and len(data) == 4 and data[0] == 0x54:
            register, value, checksum = data[1:]
            pending_register = None
            if register not in (0x0D, 0x00, 0x0C):
                continue
            if not valid_checksum(register, value, checksum):
                checksum_errors += 1
                continue
            writes.append({"t_s": t_s, "register": register, "value": value})

    cycles: list[dict] = []
    read_cursor = 0
    current_reads: dict[int, dict] = {}
    for write_index, item in enumerate(writes):
        if item["register"] != 0x0D:
            continue
        following = writes[write_index + 1:write_index + 3]
        write_values = {entry["register"]: entry for entry in following}
        if 0x00 not in write_values or 0x0C not in write_values:
            continue
        if write_values[0x0C]["t_s"] - item["t_s"] > 0.1:
            continue

        while read_cursor < len(reads) and reads[read_cursor]["t_s"] < item["t_s"]:
            read = reads[read_cursor]
            current_reads[read["register"]] = read
            read_cursor += 1

        row = {
            "index": len(cycles),
            "t_s": item["t_s"],
            "w0d": item["value"],
            "topology": TOPOLOGY_NAMES.get(item["value"], f"0x{item['value']:02X}"),
            "w00": write_values[0x00]["value"],
            "gear": write_values[0x0C]["value"],
            "w00_t_s": write_values[0x00]["t_s"],
            "gear_t_s": write_values[0x0C]["t_s"],
        }
        for register in READ_REGISTERS:
            read = current_reads.get(register)
            row[f"r{register:02x}"] = read["value"] if read else None
            row[f"r{register:02x}_t_s"] = read["t_s"] if read else None
        cycles.append(row)

    command_transitions: list[dict] = []
    feedback_transitions: list[dict] = []
    for previous, current in zip(cycles, cycles[1:]):
        old_command = (previous["w0d"], previous["w00"], previous["gear"])
        new_command = (current["w0d"], current["w00"], current["gear"])
        if old_command != new_command:
            command_transitions.append({
                "t_s": current["t_s"],
                "from": f"{previous['topology']}/{previous['w00']}/{previous['gear']}",
                "to": f"{current['topology']}/{current['w00']}/{current['gear']}",
                "w0d_to_w00_ms": round((current["w00_t_s"] - current["t_s"]) * 1000, 3),
                "w00_to_w0c_ms": round((current["gear_t_s"] - current["w00_t_s"]) * 1000, 3),
            })
        for register in READ_REGISTERS:
            key = f"r{register:02x}"
            if previous[key] != current[key]:
                feedback_transitions.append({
                    "t_s": current[f"r{register:02x}_t_s"],
                    "register": f"R{register:02X}",
                    "from": previous[key],
                    "to": current[key],
                    "command_at_cycle": f"{current['topology']}/{current['w00']}/{current['gear']}",
                })

    starts: list[dict] = []
    stops: list[dict] = []
    for previous, current in zip(cycles, cycles[1:]):
        previous_active = previous["w00"] == 1 and previous["gear"] > 0 and previous["w0d"] != 0
        current_active = current["w00"] == 1 and current["gear"] > 0 and current["w0d"] != 0
        if not previous_active and current_active:
            next_inactive = next((
                cycle for cycle in cycles[current["index"] + 1:]
                if not (cycle["w00"] == 1 and cycle["gear"] > 0 and cycle["w0d"] != 0)
            ), None)
            active_end_t_s = next_inactive["t_s"] if next_inactive else float("inf")
            event = {
                "command_t_s": current["t_s"],
                "initial_topology": current["topology"],
                "initial_gear": current["gear"],
                "previous_command": f"{previous['topology']}/{previous['w00']}/{previous['gear']}",
                "r26_before": previous["r26"],
                "r27_before": previous["r27"],
            }
            r26_reads = [read for read in reads if read["register"] == 0x26]
            first_r26 = next((
                read for read in r26_reads
                if current["t_s"] <= read["t_s"] < active_end_t_s and read["value"] != 0
            ), None)
            last_r26_zero = next((
                read for read in reversed(r26_reads)
                if first_r26 and read["t_s"] < first_r26["t_s"] and read["value"] == 0
            ), None)
            event["last_r26_zero_t_s"] = last_r26_zero["t_s"] if last_r26_zero else None
            event["first_r26_t_s"] = first_r26["t_s"] if first_r26 else None
            event["first_r26_value"] = first_r26["value"] if first_r26 else None
            event["r26_activation_lower_bound_s"] = (
                round(last_r26_zero["t_s"] - current["t_s"], 6) if last_r26_zero else None
            )
            event["r26_activation_upper_bound_s"] = (
                round(first_r26["t_s"] - current["t_s"], 6) if first_r26 else None
            )

            for register, threshold in ((0x21, 10), (0x27, 1)):
                first = next((
                    read for read in reads
                    if read["register"] == register
                    and current["t_s"] <= read["t_s"] < active_end_t_s
                    and read["value"] >= threshold
                ), None)
                event[f"first_r{register:02x}_t_s"] = first["t_s"] if first else None
                event[f"first_r{register:02x}_value"] = first["value"] if first else None
                event[f"r{register:02x}_delay_s"] = (
                    round(first["t_s"] - current["t_s"], 6) if first else None
                )
            starts.append(event)
        elif previous_active and not current_active:
            first_off = next((
                cycle for cycle in cycles[current["index"]:]
                if cycle["r26"] == 0
            ), None)
            stops.append({
                "command_t_s": current["t_s"],
                "previous_topology": previous["topology"],
                "previous_gear": previous["gear"],
                "first_r26_zero_t_s": first_off["r26_t_s"] if first_off else None,
                "r26_off_delay_s": (
                    round(first_off["r26_t_s"] - current["t_s"], 6) if first_off else None
                ),
            })

    periods = [current["t_s"] - previous["t_s"] for previous, current in zip(cycles, cycles[1:])]
    result = {
        "capture": decoded["source"],
        "duration_s": decoded["duration_s"],
        "decoded_frames": len(frames),
        "feedback_reads": len(reads),
        "control_writes": len(writes),
        "heartbeat_cycles": len(cycles),
        "checksum_errors": checksum_errors,
        "heartbeat_period_median_s": statistics.median(periods),
        "heartbeat_period_min_s": min(periods),
        "heartbeat_period_max_s": max(periods),
        "starts": starts,
        "stops": stops,
        "command_transitions": command_transitions,
        "feedback_transitions": feedback_transitions,
    }
    args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    csv_fields = [
        "index", "t_s", "topology", "w0d", "w00", "gear",
        *(f"r{register:02x}" for register in READ_REGISTERS),
    ]
    with args.csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=csv_fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(cycles)

    print(json.dumps({
        "duration_s": result["duration_s"],
        "decoded_frames": result["decoded_frames"],
        "heartbeat_cycles": result["heartbeat_cycles"],
        "checksum_errors": result["checksum_errors"],
        "heartbeat_period_median_s": result["heartbeat_period_median_s"],
        "starts": starts,
        "stops": stops,
    }, ensure_ascii=False, indent=2))
    print("COMMAND TRANSITIONS")
    for event in command_transitions:
        print(f"{event['t_s']:10.6f}s {event['from']:>12} -> {event['to']:<12}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
