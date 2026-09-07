#!/usr/bin/env python3
"""Audit stock-firmware R21 traffic and correlate it with commanded power.

The input directories are private passive captures.  This script is read-only: it
loads decoded ``i2c_report.json`` files, reconstructs the last complete W0D/W00/W0C
command, and summarizes checksum-valid R21 replies after a settling interval.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class Sample:
    capture: str
    time_s: float
    value: int
    command_0d: int | None
    command_00: int | None
    command_0c: int | None
    command_age_s: float | None
    r20: int | None
    r26: int | None
    r27: int | None


def report_frames(path: Path) -> list[dict[str, Any]]:
    report = json.loads(path.read_text(encoding="utf-8"))
    if "best" in report:
        return report["best"]["frames"]
    mappings = report.get("mappings", [])
    if not mappings:
        raise ValueError(f"no decoded mappings in {path}")
    return mappings[0]["frames"]


def checksum_valid(register: int, value: int, checksum: int) -> bool:
    return ((register + value) & 0xFF) == checksum


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return float(ordered[0])
    at = (len(ordered) - 1) * fraction
    lower = int(at)
    upper = min(lower + 1, len(ordered) - 1)
    weight = at - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def decode_capture(
    path: Path,
) -> tuple[list[Sample], list[dict[str, Any]], dict[int, Counter[int]]]:
    frames = report_frames(path)
    selected_register: int | None = None
    selected_at_s = 0.0
    command: dict[int, int] = {}
    command_tuple: tuple[int, int, int] | None = None
    command_changed_at_s: float | None = None
    latest: dict[int, int] = {}
    samples: list[Sample] = []
    anomalies: list[dict[str, Any]] = []
    register_values: dict[int, Counter[int]] = defaultdict(Counter)

    for frame in frames:
        data = frame.get("bytes", [])
        direction = frame.get("direction")
        time_s = float(frame.get("start_s", 0.0))
        if direction == "write" and len(data) == 2 and data[0] == 0x54:
            register = data[1]
            if 0x20 <= register <= 0x2F:
                selected_register = register
                selected_at_s = time_s
            continue
        if direction == "write" and len(data) == 4 and data[0] == 0x54:
            register, value, checksum = data[1:]
            if register in (0x0D, 0x00, 0x0C) and checksum_valid(register, value, checksum):
                command[register] = value
                if all(register in command for register in (0x0D, 0x00, 0x0C)):
                    candidate = (command[0x0D], command[0x00], command[0x0C])
                    if candidate != command_tuple:
                        command_tuple = candidate
                        command_changed_at_s = time_s
            continue
        if direction != "read" or not data or data[0] != 0x55 or selected_register is None:
            continue
        # A normal reply begins with the read address and carries value+checksum.
        if len(data) != 3:
            if selected_register == 0x21:
                anomalies.append({
                    "capture": path.parent.name,
                    "time_s": time_s,
                    "register": "0x21",
                    "bytes": " ".join(f"{value:02X}" for value in data),
                    "reason": "truncated_or_unpaired_reply",
                })
            selected_register = None
            continue
        value, checksum = data[1], data[2]
        if not checksum_valid(selected_register, value, checksum):
            if selected_register == 0x21:
                anomalies.append({
                    "capture": path.parent.name,
                    "time_s": time_s,
                    "register": "0x21",
                    "bytes": " ".join(f"{part:02X}" for part in data),
                    "reason": "bad_checksum",
                })
            selected_register = None
            continue
        latest[selected_register] = value
        register_values[selected_register][value] += 1
        if selected_register == 0x21:
            cmd = command_tuple or (None, None, None)
            samples.append(Sample(
                capture=path.parent.name,
                time_s=time_s,
                value=value,
                command_0d=cmd[0],
                command_00=cmd[1],
                command_0c=cmd[2],
                command_age_s=(None if command_changed_at_s is None else
                               time_s - command_changed_at_s),
                r20=latest.get(0x20),
                r26=latest.get(0x26),
                r27=latest.get(0x27),
            ))
        selected_register = None
    return samples, anomalies, register_values


def stable_power_samples(samples: list[Sample], settle_s: float) -> list[Sample]:
    return [
        sample for sample in samples
        if sample.command_00 == 1
        and sample.command_0d in (0xA1, 0xC1, 0xE1)
        and sample.command_0c is not None
        and sample.command_0c > 0
        and sample.command_age_s is not None
        and sample.command_age_s >= settle_s
        and sample.r20 == 0
        and sample.r26 in (1, 2)
    ]


def power_rows(samples: list[Sample]) -> list[dict[str, Any]]:
    grouped: dict[int, list[Sample]] = defaultdict(list)
    for sample in samples:
        assert sample.command_0c is not None
        grouped[sample.command_0c].append(sample)
    rows = []
    for power, group in sorted(grouped.items()):
        values = [sample.value for sample in group]
        rows.append({
            "power": power,
            "samples": len(values),
            "captures": len({sample.capture for sample in group}),
            "r21_min": min(values),
            "r21_p10": round(percentile(values, 0.10), 2),
            "r21_median": round(statistics.median(values), 2),
            "r21_mean": round(statistics.fmean(values), 2),
            "r21_p90": round(percentile(values, 0.90), 2),
            "r21_max": max(values),
            "r26_values": "/".join(f"{value:02X}" for value in
                                     sorted({sample.r26 for sample in group
                                             if sample.r26 is not None})),
            "r27_values": "/".join(f"{value:02X}" for value in
                                     sorted({sample.r27 for sample in group
                                             if sample.r27 is not None})),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_root", type=Path)
    parser.add_argument("--settle-s", type=float, default=5.0)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    reports = sorted(args.capture_root.glob("i2c_*/i2c_report.json"))
    if not reports:
        raise SystemExit(f"no i2c_report.json files under {args.capture_root}")

    all_samples: list[Sample] = []
    all_anomalies: list[dict[str, Any]] = []
    register_histograms: dict[int, Counter[int]] = defaultdict(Counter)
    capture_rows = []
    for report in reports:
        samples, anomalies, histograms = decode_capture(report)
        all_samples.extend(samples)
        all_anomalies.extend(anomalies)
        for register, histogram in histograms.items():
            register_histograms[register].update(histogram)
        capture_rows.append({
            "capture": report.parent.name,
            "r21_valid": len(samples),
            "r21_min": min((sample.value for sample in samples), default=None),
            "r21_max": max((sample.value for sample in samples), default=None),
            "r21_anomalies": len(anomalies),
        })

    stable = stable_power_samples(all_samples, args.settle_s)
    rows = power_rows(stable)
    output = {
        "settle_s": args.settle_s,
        "capture_count": len(reports),
        "r21_valid_samples": len(all_samples),
        "r21_distinct_values": len({sample.value for sample in all_samples}),
        "r21_min": min(sample.value for sample in all_samples),
        "r21_max": max(sample.value for sample in all_samples),
        "r21_anomalies": all_anomalies,
        "captures": capture_rows,
        "decoded_register_counts": {
            f"R{register:02X}": sum(register_histograms[register].values())
            for register in sorted(register_histograms)
        },
        "decoded_register_values": {
            f"R{register:02X}": {
                "distinct": len(histogram),
                "min": min(histogram),
                "max": max(histogram),
                "values": ([f"{value:02X}" for value in sorted(histogram)]
                           if len(histogram) <= 16 else None),
                "most_common": [
                    {"value": f"{value:02X}", "samples": count}
                    for value, count in histogram.most_common(8)
                ],
            }
            for register, histogram in sorted(register_histograms.items())
        },
        "stable_power_rows": rows,
    }

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]) if rows else ["power"])
            writer.writeheader()
            writer.writerows(rows)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(output, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
