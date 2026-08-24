#!/usr/bin/env python3
"""Correlate decoded MCL02M I2C frames with read-only SOC telemetry."""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import math
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path
from typing import Iterable


def nearest(points: list[tuple[float, int]], target: float) -> int | None:
    if not points:
        return None
    times = [item[0] for item in points]
    index = bisect.bisect_left(times, target)
    candidates = []
    if index < len(points):
        candidates.append(points[index])
    if index:
        candidates.append(points[index - 1])
    return min(candidates, key=lambda item: abs(item[0] - target))[1]


def pearson(xs: Iterable[float], ys: Iterable[float]) -> float | None:
    x = list(xs)
    y = list(ys)
    if len(x) != len(y) or len(x) < 2:
        return None
    xm = sum(x) / len(x)
    ym = sum(y) / len(y)
    dx = [value - xm for value in x]
    dy = [value - ym for value in y]
    denom = math.sqrt(sum(value * value for value in dx) * sum(value * value for value in dy))
    if not denom:
        return None
    return sum(a * b for a, b in zip(dx, dy)) / denom


def linear_fit(xs: Iterable[float], ys: Iterable[float]) -> dict[str, float] | None:
    x = list(xs)
    y = list(ys)
    if len(x) != len(y) or len(x) < 2:
        return None
    xm = sum(x) / len(x)
    ym = sum(y) / len(y)
    variance = sum((value - xm) ** 2 for value in x)
    if not variance:
        return None
    slope = sum((a - xm) * (b - ym) for a, b in zip(x, y)) / variance
    intercept = ym - slope * xm
    rmse = math.sqrt(sum((slope * a + intercept - b) ** 2 for a, b in zip(x, y)) / len(x))
    return {"slope": slope, "intercept": intercept, "rmse_c": rmse}


def changes(points: list[tuple[float, int]]) -> list[dict[str, float | int | str]]:
    result = []
    previous = None
    for time_s, value in points:
        if value != previous:
            result.append({"time_s": round(time_s, 6), "value": value, "value_hex": f"0x{value:02X}"})
            previous = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("i2c_report", type=Path)
    parser.add_argument("soc_jsonl", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-csv", type=Path, required=True)
    args = parser.parse_args()

    report = json.loads(args.i2c_report.read_text(encoding="utf-8"))["best"]
    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    capture_start = datetime.fromisoformat(metadata["capture"]["started_at"])
    soc = [json.loads(line) for line in args.soc_jsonl.read_text(encoding="utf-8").splitlines() if line]

    reads: dict[int, list[tuple[float, int]]] = defaultdict(list)
    writes: dict[int, list[tuple[float, int]]] = defaultdict(list)
    pending_register = None
    checksum_errors = []
    for frame in report["frames"]:
        payload = frame["bytes"]
        time_s = float(frame["start_s"])
        if len(payload) == 2 and payload[0] == 0x54:
            pending_register = payload[1]
        elif len(payload) == 3 and payload[0] == 0x55 and pending_register is not None:
            value, checksum = payload[1], payload[2]
            if checksum != ((value + pending_register) & 0xFF):
                checksum_errors.append({"time_s": time_s, "register": pending_register, "value": value})
            reads[pending_register].append((time_s, value))
            pending_register = None
        elif len(payload) == 4 and payload[0] == 0x54:
            register, value, checksum = payload[1], payload[2], payload[3]
            if checksum != ((register + value) & 0xFF):
                checksum_errors.append({"time_s": time_s, "register": register, "value": value})
            writes[register].append((time_s, value))
            pending_register = None
        else:
            pending_register = None

    rows = []
    for sample in soc:
        capture_s = (datetime.fromisoformat(sample["timestamp"]) - capture_start).total_seconds()
        # A telemetry log may contain samples from adjacent captures.  Do not
        # snap out-of-window samples to the first or last I2C observation.
        if capture_s < 0 or capture_s > float(report["duration_s"]):
            continue
        row = {
            "timestamp": sample["timestamp"],
            "capture_s": round(capture_s, 6),
            "status": sample["status"],
            "fault": sample["fault"],
            "on": sample["on"],
            "heat_level": sample["heat_level"],
            "bottom_temperature_c": sample["bottom_temperature"],
            "igbt_c": sample["igbt_c"],
            "error_code": sample["error_code"],
        }
        for register in range(0x20, 0x28):
            row[f"r{register:02x}_raw"] = nearest(reads[register], capture_s)
        rows.append(row)

    if not rows:
        raise ValueError("no SOC samples fall inside the capture time window")
    fieldnames = list(rows[0])
    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.output_csv.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    bottom = [float(row["bottom_temperature_c"]) for row in rows]
    igbt = [float(row["igbt_c"]) for row in rows]
    correlations = {}
    for register in range(0x20, 0x28):
        raw = [float(row[f"r{register:02x}_raw"]) for row in rows]
        correlations[f"0x{register:02X}"] = {
            "raw_min": int(min(raw)),
            "raw_max": int(max(raw)),
            "correlation_bottom": pearson(raw, bottom),
            "correlation_igbt": pearson(raw, igbt),
            "fit_bottom": linear_fit(raw, bottom),
            "fit_igbt": linear_fit(raw, igbt),
        }

    output = {
        "capture": {
            "id": metadata["capture"]["capture_id"],
            "duration_s": report["duration_s"],
            "mapping": report["mapping"],
            "frames": len(report["frames"]),
            "starts": report["starts"],
            "stops": report["stops"],
            "decoded_bytes": report["decoded_bytes"],
            "valid_ack_bits": report["valid_ack_bits"],
            "checksum_errors": len(checksum_errors),
        },
        "write_changes": {f"0x{register:02X}": changes(points) for register, points in sorted(writes.items())},
        "read_changes": {
            f"0x{register:02X}": changes(reads[register])
            for register in (0x20, 0x25, 0x26, 0x27)
        },
        "read_value_counts": {
            f"0x{register:02X}": dict(sorted(Counter(value for _, value in points).items()))
            for register, points in sorted(reads.items())
        },
        "correlations": correlations,
    }
    args.output_json.write_text(json.dumps(output, indent=2), encoding="utf-8")
    print(json.dumps(output["capture"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
