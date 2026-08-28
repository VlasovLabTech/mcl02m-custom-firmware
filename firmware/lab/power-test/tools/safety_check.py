#!/usr/bin/env python3
"""Static safety/contract gate for the MCL02M power-stage test image."""

from __future__ import annotations

import csv
import hashlib
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)
    print(f"PASS: {message}")


def macro_value(source: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(0x[0-9a-fA-F]+|\d+)U?\s*$",
                      source, re.MULTILINE)
    if match is None:
        fail(f"missing numeric macro {name}")
    return int(match.group(1), 0)


def function_body(source: str, name: str) -> str:
    start = source.find(f"{name}(")
    if start < 0:
        fail(f"missing function {name}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    fail(f"unterminated function {name}")
    return ""


def main() -> int:
    control = (MAIN / "powerboard_control.c").read_text(encoding="utf-8")
    safety = (MAIN / "safety.h").read_text(encoding="utf-8")
    web = (MAIN / "web_server_power.c").read_text(encoding="utf-8")
    app = (MAIN / "app_main.c").read_text(encoding="utf-8")
    display = (MAIN / "status_display.c").read_text(encoding="utf-8")
    control_header = (MAIN / "powerboard_control.h").read_text(encoding="utf-8")
    all_source = "\n".join((control, control_header, safety, web, app, display))

    require(macro_value(safety, "MCL02M_MAX_GEAR") == 99,
            "gear is hard-limited to 99")
    require(macro_value(safety, "MCL02M_MAX_RUN_MS") == 300_000,
            "each run is hard-limited to five minutes")
    require(macro_value(safety, "MCL02M_MAX_IGBT_C") == 80,
            "indicated IGBT temperature limit is 80 C")
    require(macro_value(safety, "MCL02M_MAX_BOTTOM_C") == 120,
            "indicated bottom NTC temperature limit is 120 C")
    require(macro_value(safety, "MCL02M_MAX_HEARTBEAT_GAP_MS") == 5_000,
            "heartbeat-gap experiment is limited to five seconds")
    require(macro_value(safety, "MCL02M_CONTROL_HEARTBEAT_MS") == 500,
            "logical power-board commands are emitted on a 500-ms heartbeat")
    require(macro_value(safety, "MCL02M_GEAR_STEP_PER_HEARTBEAT") == 10,
            "gear ramp changes at most ten levels per heartbeat within one topology")
    require(macro_value(safety, "MCL02M_LOW_TOPOLOGY_MAX_GEAR") == 35 and
            macro_value(safety, "MCL02M_HIGH_TOPOLOGY_MIN_GEAR") == 56,
            "low and high topology boundaries are explicit")
    require(macro_value(safety, "MCL02M_START_CONFIRM_TIMEOUT_MS") == 8_000,
            "R26 startup confirmation timeout is eight seconds")
    require(macro_value(safety, "MCL02M_R20_TRANSITION_MAX_SAMPLES") == 20,
            "verified R20=0x2B relay transition is bounded to ten seconds")

    literal_writes = {
        int(value, 0)
        for value in re.findall(r"write_register\s*\(\s*(0x[0-9a-fA-F]+|\d+)", control)
    }
    require(literal_writes == {0x00, 0x0C, 0x0D},
            "only known control registers 00/0C/0D are written")
    require("reg + value" in control and "reg + response[0]" in control,
            "write and read checksums are enforced")
    require("mcl02m_powerboard_control_register_allowed(reg)" in control,
            "runtime register allow-list guards every write")

    stop = function_body(control, "send_stop_sequence")
    expected_stop = ("write_register(0x0d, 0x00)",
                     "write_register(0x00, 0x00)",
                     "write_register(0x0c, 0x00)")
    offsets = [stop.find(fragment) for fragment in expected_stop]
    require(all(offset >= 0 for offset in offsets) and offsets == sorted(offsets),
            "Stop sends 0D=0, 00=0, 0C=0 in order")
    boot_stop = control.find("esp_err_t boot_stop = send_stop_sequence();")
    boot_probe = control.find("esp_err_t startup = startup_probe();")
    require(0 <= boot_stop < boot_probe, "boot issues Stop before probing the power board")

    require("if (!interrupted && !wait_until_or_stop(cycle_start, 400))" in control and
            "if (!wait_until_or_stop(cycle_start, 450))" in control and
            "if (!wait_until_or_stop(cycle_start, 454))" in control,
            "remote/fault Stop interrupts every heartbeat-write boundary")
    require("s_status.applied_gear > 10" in control and
            "MCL02M_MAX_HEARTBEAT_GAP_MS" in control,
            "heartbeat gap is accepted only at gear 10 or below")
    gear_body = function_body(control, "powerboard_control_set_gear")
    require("write_register" not in gear_body and
            "s_status.target_gear = new_gear" in gear_body,
            "rapid gear requests only replace the pending target")
    ramp_body = function_body(control, "advance_ramp")
    require("next_ramped_gear" in ramp_body and
            "s_status.topology = topology_for_gear" in ramp_body and
            "s_force_stop" not in ramp_body,
            "gear/topology changes occur directly once per heartbeat without interface Stop")
    ramp_step = function_body(control, "next_ramped_gear")
    require("candidate = MCL02M_HIGH_TOPOLOGY_MIN_GEAR" in ramp_step and
            "candidate = MCL02M_LOW_TOPOLOGY_MAX_GEAR" in ramp_step and
            "MCL02M_GEAR_STEP_PER_HEARTBEAT" in ramp_step,
            "ramps skip 36..55 only when crossing directly between low and high topologies")
    control_task = function_body(control, "control_task")
    require("vTaskDelayUntil(&next, pdMS_TO_TICKS(MCL02M_CONTROL_HEARTBEAT_MS))" in control_task,
            "control task is paced by the verified 500-ms stock heartbeat")
    feedback = function_body(control, "update_status_feedback")
    require("s_status.state == PB_STATE_STARTING" in feedback and
            "r26_valid && r26 == 0x02" in feedback and
            "s_status.state = PB_STATE_HEATING" in feedback and
            control.count("s_status.state = PB_STATE_HEATING") == 1,
            "STARTING becomes HEATING only after valid R26=02 feedback")
    require("r20 == 0x2b" in feedback and
            "MCL02M_R20_TRANSITION_MAX_SAMPLES" in feedback and
            "fault_locked(\"POWER TRANSITION\")" in feedback,
            "verified R20=0x2B is treated as a bounded relay-transition status")
    require("MCL02M_START_CONFIRM_TIMEOUT_MS" in control_task and
            "fault_locked(\"START TIMEOUT\")" in control and
            control_task.find("write_register(0x0c") <
            control_task.find("s_start_confirm_deadline_us = esp_timer_get_time()"),
            "startup confirmation watchdog begins after the command heartbeat")
    require(control_task.find("update_status_feedback()") <
            control_task.find("update_time_and_safety(esp_timer_get_time())"),
            "fresh R26/NoPan feedback is processed before startup timeout evaluation")
    require("PB_STATE_TRANSITION" not in all_source and
            "MCL02M_TOPOLOGY_DEADTIME_MS" not in all_source,
            "obsolete interface-side relay dead-time state is removed")
    require("PB_STATE_ARMED" in control and "s_arm_deadline_us" in control and
            "s_run_deadline_us" in control,
            "heating requires a timed Arm and a bounded run deadline")
    timing = function_body(control, "update_time_and_safety")
    require("stop_locked(\"COMPLETE\")" in timing and "RUN TIMEOUT" not in control,
            "normal run-duration expiry performs a safe completed Stop, not a fault")
    require("MCL02M_MAX_IGBT_C" in control and "MCL02M_MAX_BOTTOM_C" in control and
            "MCL02M_I2C_BAD_CYCLES_TO_FAULT" in control,
            "temperature and I2C-loss faults are active")

    stop_web = function_body(web, "stop_handler")
    require("require_token" not in stop_web and "powerboard_control_stop" in stop_web,
            "network Stop remains available without a token")
    for guarded in ("arm_handler", "start_handler", "gear_handler", "pause_handler",
                    "resume_handler", "heartbeat_gap_handler", "clear_fault_handler"):
        require("require_token" in function_body(web, guarded),
                f"{guarded} is token-guarded")
    require("ui_oled_show_text" in display and "igbt_c" in display and
            "bottom_c" in display and "applied_gear" in display and "PWR:%u.%uS" in display,
            "OLED shows state, gear, temperatures and R26 confirmation wait")

    forbidden = (
        "esp_efuse_write", "esp_efuse_batch_write", "esp_flash_erase",
        "esp_partition_erase", "esp_ota_begin", "esp_ota_write", "nvs_set_",
        "nvs_commit", "esp_restart",
    )
    found_forbidden = [symbol for symbol in forbidden if symbol in all_source]
    require(not found_forbidden,
            "application contains no eFuse, flash erase/write, OTA, NVS write or restart API")

    expected_partitions = {
        "miio_fw1": (0x10000, 0x160000),
        "miio_fw2": (0x170000, 0x160000),
        "mimcu": (0x2E3000, 0x100000),
    }
    actual: dict[str, tuple[int, int]] = {}
    with (ROOT / "partitions.csv").open(newline="", encoding="utf-8") as handle:
        rows = (line for line in handle if not line.lstrip().startswith("#"))
        for row in csv.reader(rows, skipinitialspace=True):
            if not row or not row[0].strip():
                continue
            actual[row[0].strip()] = (int(row[3].strip(), 0), int(row[4].strip(), 0))
    require(all(actual.get(name) == layout for name, layout in expected_partitions.items()),
            "build uses the verified stock OTA/data offsets")

    binary = ROOT / "build" / "mcl02m_power_test.bin"
    require(binary.is_file(), "application binary exists")
    size = binary.stat().st_size
    require(size <= expected_partitions["miio_fw2"][1],
            f"application fits one stock OTA slot ({size:#x} <= 0x160000)")
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    print(f"INFO: binary={binary}")
    print(f"INFO: size={size} bytes")
    print(f"INFO: sha256={digest}")
    print("SAFETY CHECK: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
