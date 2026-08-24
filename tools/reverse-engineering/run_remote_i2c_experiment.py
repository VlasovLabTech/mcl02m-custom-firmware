#!/usr/bin/env python3
"""Bounded remote MCL02M experiment with continuous read-only telemetry.

This script is intentionally narrow: it uses only the documented MIoT
properties On (2.3) and HeatLevel (2.7), plus read-only properties and the
documented get-selfcheck action (3.15).  It always attempts HeatLevel=0 and
On=false in a finally block.  No raw I2C or firmware command is sent.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from typing import Any

from diagnose_cooker import (
    SELFCHECK_COMMANDS,
    extract_selfcheck_value,
    load_dotenv,
    connection_from_env,
)
from miio.integrations.genericmiot.genericmiot import GenericMiot


PROPERTY_QUERY = [
    {"did": "status", "siid": 2, "piid": 1},
    {"did": "fault", "siid": 2, "piid": 2},
    {"did": "on", "siid": 2, "piid": 3},
    {"did": "heat_level", "siid": 2, "piid": 7},
    {"did": "bottom_temperature", "siid": 3, "piid": 7},
    {"did": "error_code", "siid": 3, "piid": 13},
]

# Durations sum to exactly 300 seconds.  The highest automatic level is 55;
# level 77/99 was already captured manually and is intentionally not repeated
# during a long thermal run.
PHASES = [
    ("standby_baseline", 0, 20.0),
    ("level_1", 1, 35.0),
    ("level_5", 5, 35.0),
    ("level_15", 15, 45.0),
    ("level_30", 30, 60.0),
    ("level_55", 55, 55.0),
    ("level_15_return", 15, 30.0),
    ("cooldown_off", 0, 20.0),
]


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def read_properties(device: GenericMiot) -> dict[str, Any]:
    response = device.send("get_properties", PROPERTY_QUERY)
    return {
        str(item.get("did")): item.get("value") if item.get("code", 0) == 0 else None
        for item in response
    }


def read_selfchecks(device: GenericMiot, commands: list[int]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, command in SELFCHECK_COMMANDS.items():
        if command not in commands:
            continue
        response = device.call_action_by(3, 15, [{"piid": 36, "value": command}])
        result[name] = {
            "command": command,
            "value": extract_selfcheck_value(response),
            "code": response.get("code") if isinstance(response, dict) else None,
        }
    return result


def write_event(handle, event: str, **fields: Any) -> None:
    handle.write(json.dumps({"timestamp": now(), "kind": "event", "event": event, **fields}, ensure_ascii=False, default=str) + "\n")
    handle.flush()


def set_level(device: GenericMiot, level: int, handle) -> Any:
    response = device.set_property_by(2, 7, level)
    write_event(handle, "set_heat_level", level=level, response=response)
    return response


def set_on(device: GenericMiot, value: bool, handle) -> Any:
    response = device.set_property_by(2, 3, value)
    write_event(handle, "set_on", value=value, response=response)
    return response


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--sample-interval", type=float, default=1.0)
    ap.add_argument("--igbt-limit", type=float, default=80.0)
    args = ap.parse_args()
    if args.sample_interval <= 0:
        raise ValueError("sample interval must be positive")

    load_dotenv(Path(__file__).with_name(".env"))
    connection = connection_from_env()
    device = GenericMiot(ip=connection.ip, token=connection.token, model=connection.model, timeout=5)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    start = time.monotonic()
    deadline = start + sum(duration for _, _, duration in PHASES)
    phase_index = -1
    phase_end = start
    emergency = False
    sample = 0

    with args.output.open("w", encoding="utf-8", newline="\n") as handle:
        write_event(handle, "experiment_start", phases=PHASES, igbt_limit=args.igbt_limit)
        try:
            for phase_index, (name, level, duration) in enumerate(PHASES):
                phase_started = time.monotonic()
                phase_end = phase_started + duration
                write_event(handle, "phase_start", phase=name, target_level=level, duration_s=duration)

                if level == 0:
                    set_level(device, 0, handle)
                    set_on(device, False, handle)
                else:
                    # Set a safe low target before enabling, then apply the
                    # requested phase level.  The first phase is level 1.
                    if name == "level_1":
                        set_level(device, 1, handle)
                        set_on(device, True, handle)
                    else:
                        set_level(device, level, handle)

                boundary = read_selfchecks(device, [1, 3, 4, 5])
                write_event(handle, "phase_selfchecks", phase=name, selfchecks=boundary)

                while time.monotonic() < phase_end:
                    loop_start = time.monotonic()
                    properties = read_properties(device)
                    igbt_response = read_selfchecks(device, [4]).get("igbt", {})
                    row = {
                        "timestamp": now(),
                        "kind": "sample",
                        "sample": sample,
                        "phase": name,
                        "phase_elapsed_s": round(loop_start - phase_started, 3),
                        "experiment_elapsed_s": round(loop_start - start, 3),
                        "properties": properties,
                        "igbt_c": igbt_response.get("value"),
                        "igbt_response": igbt_response,
                    }
                    handle.write(json.dumps(row, ensure_ascii=False, default=str) + "\n")
                    handle.flush()
                    sample += 1

                    fault = properties.get("fault")
                    error = properties.get("error_code")
                    igbt = igbt_response.get("value")
                    if fault not in (None, 0) or error not in (None, 0):
                        write_event(handle, "safety_stop", reason="fault_or_error", fault=fault, error_code=error)
                        emergency = True
                        return 2
                    if isinstance(igbt, (int, float)) and igbt >= args.igbt_limit:
                        write_event(handle, "safety_stop", reason="igbt_limit", igbt_c=igbt)
                        emergency = True
                        return 3

                    sleep_for = args.sample_interval - (time.monotonic() - loop_start)
                    if sleep_for > 0:
                        time.sleep(min(sleep_for, max(0.0, phase_end - time.monotonic())))
                write_event(handle, "phase_end", phase=name)
            write_event(handle, "experiment_complete")
            return 0
        finally:
            # Always leave the cooker in a non-heating state, including on a
            # network/keyboard exception.  This is a documented, reversible
            # property sequence, not a raw command.
            try:
                set_level(device, 0, handle)
                set_on(device, False, handle)
                write_event(handle, "safe_off_complete", emergency=emergency)
            except Exception as exc:
                write_event(handle, "safe_off_failed", error_type=type(exc).__name__, error=str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
