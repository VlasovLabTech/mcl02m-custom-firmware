#!/usr/bin/env python3
"""Compile and exercise real cooking/driver C code with mock RTOS/time/I2C.

Requires a native C compiler (gcc/clang or --zig PATH). No device is accessed.
The fixtures model successful complete command transmission and scheduling;
feedback parsing, transitions, faults, controller and engine use the real sources.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "tests" / "host"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", type=Path)
    args = parser.parse_args()
    compiler = [str(args.zig), "cc"] if args.zig else [shutil.which("gcc") or shutil.which("clang") or "cc"]
    sources = [HOST / name for name in ("host_runtime.c", "power_fixture.c", "engine_fixture.c", "scenarios.c")]
    sources.append(ROOT / "main" / "temperature_ctrl.c")
    flags = ["-std=c11", "-O0", "-g", "-include", str(HOST / "host_runtime.h")]
    for path in (HOST, ROOT / "main", ROOT.parent / "lab" / "power-test" / "main", ROOT.parent / "lab" / "ui-test" / "main"):
        flags += ["-I", str(path)]
    # Match the production component's configured protection and session policy.
    for definition in ("MCL02M_POWER_TEST_BUILD=0", "MCL02M_MAX_RUN_MS=28800000U",
                       "MCL02M_MAX_IGBT_C=98U", "MCL02M_IGBT_START_INHIBIT_C=80U",
                       "MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES=2U", "MCL02M_MAX_BOTTOM_C=210U",
                       "MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES=6U", "MCL02M_RAW_SENSOR_FAULT_SAMPLES=2U",
                       "MCL02M_ACTIVE_ZERO_ENABLED=1", "MCL02M_COOKING_LEASE_ENABLED=1"):
        flags.append("-D" + definition)
    scenarios = ("cool_pause_resume_zero", "cool_pause_resume_heat", "lower_target_during_start",
                 "first_heat_after_zero_ramp", "edited_start_topology", "pan_return_off_hold",
                 "pan_lost_after_hold", "setpoint_pan_return_race", "profile_pan_return_race",
                 "delayed_retry_after_timeout", "stale_confirmation_after_stop",
                 "stale_confirmation_after_fault", "first_fault_preserved",
                 "stop_needs_consecutive_samples", "no_pan_needs_consecutive_samples",
                 "heating_still_needs_ack", "zero_invalid_feedback", "zero_timer_complete",
                 "pause_two_hour_limit", "igbt_requires_fresh_samples", "native_fault_still_stops",
                 "i2c_loss_still_stops", "lease_loss_still_stops", "second_delayed_timeout_is_est",
                 "first_heat_after_zero_waits_eight_seconds")
    with tempfile.TemporaryDirectory(prefix="mcl02m-host-") as build:
        exe = Path(build) / "scenarios.exe"
        subprocess.run(compiler + flags + list(map(str, sources)) + ["-o", str(exe)], check=True)
        failures = []
        for scenario in scenarios:
            result = subprocess.run([str(exe), scenario], capture_output=True, text=True)
            print(result.stdout.strip() or result.stderr.strip())
            if result.returncode: failures.append(scenario)
        if failures:
            raise SystemExit("FAILED: " + ", ".join(failures))
    print(f"C HOST SCENARIOS: PASS ({len(scenarios)} scenarios)")

if __name__ == "__main__":
    main()
