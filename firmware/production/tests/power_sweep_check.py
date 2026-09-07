#!/usr/bin/env python3
"""Offline contract gate for the experiment-only cookware sweep."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parents[1]
EXPERIMENT = ROOT / "main" / "power_sweep_experiment.c"
POWER = PROJECT / "firmware" / "lab" / "power-test" / "main" / "powerboard_control.c"
RUNNER = PROJECT / "tools" / "power_sweep_runner.py"
EXPECTED = (0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 25, 30,
            35, 36, 40, 45, 50, 55, 56, 60, 70, 80, 90, 99)


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS: {message}")


def load_runner():
    spec = importlib.util.spec_from_file_location("power_sweep_runner", RUNNER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    experiment = EXPERIMENT.read_text(encoding="utf-8")
    power = POWER.read_text(encoding="utf-8")
    runner = load_runner()

    points_match = re.search(r"static const uint8_t s_points\[\] = \{(.*?)\};",
                             experiment, re.S)
    require(points_match is not None, "firmware contains an explicit sweep table")
    points = tuple(int(value) for value in re.findall(r"\b\d+\b",
                                                      points_match.group(1)))
    require(points == EXPECTED == runner.POINTS,
            "firmware and host use the requested 25-point sequence")
    require("#define SWEEP_DWELL_MS 15000U" in experiment,
            "dwell is exactly 15 seconds after settle")
    require("wait_for_power(requested, limited" in experiment and
            "output_still_matches(requested, limited)" in experiment,
            "every dwell waits for settled output and rejects local changes")
    require(all(token in experiment for token in
                ("HOST_LOST", "HOST_ABORT", "PHYSICAL_STOP", "NO_PAN", "FAULT")),
            "host loss, operator Stop, NoPan and faults abort the experiment")
    require("cooking_stop(\"SWEEP STOP\")" in experiment and
            "wait_for_idle(SWEEP_STOP_TIMEOUT_MS)" in experiment,
            "all exit paths request and wait for transactional Stop")
    require("requested == COOKER_HOLD_MAX_GEAR" in experiment and
            "power.cookware_limited" in experiment and
            'reason = "COMPLETE_LIMITED"' in experiment,
            "restricted cookware completes immediately after its P35 dwell")
    require("sweep_live_read_order" in power and
            "0x26,0x27,0x20,0x21,0x22,0x23,0x24" in power.replace(" ", "") and
            "0x25,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f" in
            power.replace(" ", ""),
            "R21 remains live while R25/R28-R2F rotate through one service slot")
    require("#if MCL02M_POWER_SWEEP_BUILD" in power,
            "extra I2C reads and telemetry are compile-time isolated")

    registers = [f"{value:02X}" for value in range(0x20, 0x30)]
    frame = ",".join([
        "X", "D", "12345", "17", "HEATING", "2", "2", "A1",
        "A1", "01", "02", "00FF", "0000", "07", "00", "FFFF",
        *registers, "63", "41", "0", "0", "0", "NONE",
    ])
    parsed = runner.parse_d(frame)
    require(parsed is not None and parsed["r21"] == "21" and
            parsed["r2f"] == "2F" and parsed["fault"] == "NONE",
            "host parser accepts the compact 38-field frame without data loss")
    require(len(runner.summary_rows([])) == len(EXPECTED),
            "an interrupted run still produces one summary row per requested point")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
