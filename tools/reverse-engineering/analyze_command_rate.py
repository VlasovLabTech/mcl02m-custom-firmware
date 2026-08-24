#!/usr/bin/env python3
"""Measure stock MCL02M control-command update rate across decoded captures."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path


def valid_checksum(register: int, value: int, checksum: int) -> bool:
    return ((register + value) & 0xFF) == checksum


def decode_cycles(path: Path) -> list[dict]:
    frames = json.loads(path.read_text(encoding="utf-8"))["best"]["frames"]
    writes: list[dict] = []
    for frame in frames:
        data = frame["bytes"]
        if frame["direction"] != "write" or len(data) != 4 or data[0] != 0x54:
            continue
        register, value, checksum = data[1:]
        if register not in (0x0D, 0x00, 0x0C) or not valid_checksum(register, value, checksum):
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
        cycles.append({
            "t_s": item["t_s"],
            "command": (item["value"], values[0x00]["value"], values[0x0C]["value"]),
        })
    return cycles


def maximum_events_in_window(times: list[float], window_s: float) -> int:
    maximum = 0
    left = 0
    for right, time_s in enumerate(times):
        while time_s - times[left] >= window_s:
            left += 1
        maximum = max(maximum, right - left + 1)
    return maximum


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    captures: list[dict] = []
    all_heartbeat_periods: list[float] = []
    all_adjacent_change_periods: list[float] = []

    for path in args.reports:
        cycles = decode_cycles(path)
        if len(cycles) < 2:
            continue
        heartbeat_periods = [
            current["t_s"] - previous["t_s"]
            for previous, current in zip(cycles, cycles[1:])
        ]
        change_times = [
            current["t_s"]
            for previous, current in zip(cycles, cycles[1:])
            if current["command"] != previous["command"]
        ]
        adjacent_change_periods = [
            cycles[index]["t_s"] - cycles[index - 1]["t_s"]
            for index in range(2, len(cycles))
            if cycles[index]["command"] != cycles[index - 1]["command"]
            and cycles[index - 1]["command"] != cycles[index - 2]["command"]
        ]
        captures.append({
            "report": str(path),
            "heartbeat_cycles": len(cycles),
            "logical_command_changes": len(change_times),
            "heartbeat_period_min_s": min(heartbeat_periods),
            "heartbeat_period_median_s": statistics.median(heartbeat_periods),
            "heartbeat_period_max_s": max(heartbeat_periods),
            "adjacent_change_interval_min_s": (
                min(adjacent_change_periods) if adjacent_change_periods else None
            ),
            "max_changes_in_strict_1s_window": maximum_events_in_window(change_times, 1.0) if change_times else 0,
        })
        all_heartbeat_periods.extend(heartbeat_periods)
        all_adjacent_change_periods.extend(adjacent_change_periods)

    result = {
        "captures": captures,
        "aggregate": {
            "capture_count": len(captures),
            "heartbeat_period_min_s": min(all_heartbeat_periods),
            "heartbeat_period_median_s": statistics.median(all_heartbeat_periods),
            "heartbeat_period_max_s": max(all_heartbeat_periods),
            "adjacent_command_change_interval_min_s": min(all_adjacent_change_periods),
            "logical_update_rate_hz_nominal": 2.0,
            "max_changes_in_strict_1s_window": max(
                capture["max_changes_in_strict_1s_window"] for capture in captures
            ),
        },
    }
    if args.json:
        args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
