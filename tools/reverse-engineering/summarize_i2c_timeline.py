#!/usr/bin/env python3
"""Summarize correlations between decoded I2C registers and SOC telemetry."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


def pearson(xs: list[float], ys: list[float]) -> float | None:
    if len(xs) != len(ys) or len(xs) < 2:
        return None
    xm = sum(xs) / len(xs)
    ym = sum(ys) / len(ys)
    dx = [value - xm for value in xs]
    dy = [value - ym for value in ys]
    denom = math.sqrt(sum(value * value for value in dx) * sum(value * value for value in dy))
    if not denom:
        return None
    return sum(a * b for a, b in zip(dx, dy)) / denom


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("timeline", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    with args.timeline.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    heating = [row for row in rows if int(row["status"]) == 2]
    levels = [float(row["heat_level"]) for row in heating]
    correlations = {}
    for register in range(0x20, 0x28):
        raw = [float(row[f"r{register:02x}_raw"]) for row in heating]
        correlations[f"0x{register:02X}"] = pearson(raw, levels)

    selected_levels = {0, 15, 30, 35, 36, 55, 56, 99}
    by_level: dict[int, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    for row in heating:
        level = int(row["heat_level"])
        if level not in selected_levels:
            continue
        for register in range(0x20, 0x28):
            by_level[level][f"0x{register:02X}"].append(float(row[f"r{register:02x}_raw"]))
    means = {
        str(level): {
            register: round(sum(values) / len(values), 3)
            for register, values in registers.items()
        }
        for level, registers in sorted(by_level.items())
    }
    output = {
        "heating_samples": len(heating),
        "correlation_with_heat_level": correlations,
        "mean_raw_by_selected_level": means,
    }
    rendered = json.dumps(output, indent=2)
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
