#!/usr/bin/env python3
"""Offline contract gate. It never opens a serial port or writes the ESP32."""

from __future__ import annotations

import csv
import hashlib
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
PROJECT_ROOT = ROOT.parents[1]
SHARED_POWER = PROJECT_ROOT / "firmware" / "lab" / "power-test" / "main"
SHARED_UI = PROJECT_ROOT / "firmware" / "lab" / "ui-test" / "main"
PRIVATE = PROJECT_ROOT / "_local_private" / "reverse-engineering" / "private-flash"
BUILD = Path(os.environ.get("MCL02M_BUILD_DIR", ROOT / "build")).resolve()
SLOT_SIZE = 0x160000
DUMP_SHA256 = "e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e"


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    files = list(MAIN.glob("*.c")) + list(MAIN.glob("*.h"))
    source = "\n".join(path.read_text(encoding="utf-8") for path in files)
    config = (MAIN / "app_config.h").read_text(encoding="utf-8")
    app_types = (MAIN / "app_types.h").read_text(encoding="utf-8")
    cmake = (MAIN / "CMakeLists.txt").read_text(encoding="utf-8")
    engine = (MAIN / "cooking_engine.c").read_text(encoding="utf-8")
    web = (MAIN / "web_server_prod.c").read_text(encoding="utf-8")
    sound = (MAIN / "sound.c").read_text(encoding="utf-8")
    private_sound = (MAIN / "private_sound_tables.c").read_text(encoding="utf-8")
    private_midi_path = (PROJECT_ROOT / "assets" / "sounds" / "midi" /
                         "generated" / "code" / "melody_tables_midi.generated.h")
    melodies_path = MAIN / "melody_tables.h"
    melodies = melodies_path.read_text(encoding="utf-8")
    nutcracker_path = MAIN / "melody_nutcracker.generated.h"
    nutcracker = nutcracker_path.read_text(encoding="utf-8")
    nutcracker_notes = [
        tuple(map(int, values))
        for values in re.findall(r"\{(\d+),\s*(\d+),\s*(\d+)\}", nutcracker)
    ]
    project_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    gitignore = (PROJECT_ROOT / ".gitignore").read_text(encoding="utf-8")
    app_main = (MAIN / "app_main.c").read_text(encoding="utf-8")
    indicators = (MAIN / "indicators.c").read_text(encoding="utf-8")
    display = (MAIN / "display_prod.c").read_text(encoding="utf-8")
    ui = (MAIN / "ui_controller.c").read_text(encoding="utf-8")
    network = (MAIN / "network_prod.c").read_text(encoding="utf-8")
    defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    settings = (MAIN / "settings.c").read_text(encoding="utf-8")
    power = (SHARED_POWER / "powerboard_control.c").read_text(encoding="utf-8")
    power_header = (SHARED_POWER / "powerboard_control.h").read_text(encoding="utf-8")
    power_safety = (SHARED_POWER / "safety.h").read_text(encoding="utf-8")
    telemetry = (SHARED_UI / "telemetry.c").read_text(encoding="utf-8")
    inputs = (SHARED_UI / "ui_inputs.c").read_text(encoding="utf-8")
    outputs = (SHARED_UI / "ui_outputs.c").read_text(encoding="utf-8")
    output_header = (SHARED_UI / "ui_outputs.h").read_text(encoding="utf-8")
    assets = (MAIN / "oled_assets.c").read_text(encoding="utf-8")
    asset_header = (MAIN / "oled_assets.h").read_text(encoding="utf-8")
    asset_generator = (ROOT / "tools" / "generate_oled_assets.py").read_text(encoding="utf-8")

    require("#define COOKER_HOLD_MAX_GEAR            35U" in config,
            "temperature HOLD cannot exceed gear 35")
    require("#define COOKER_PREHEAT_MIN_GEAR         56U" in config,
            "temperature PREHEAT begins in the agreed 56+ region")
    require("#define COOKER_NO_PAN_TIMEOUT_MS        128000U" in config and
            "#define COOKER_NO_PAN_START_FAILSAFE_MS  30000U" in config and
            "#define COOKER_NO_PAN_PLAY_FAILSAFE_MS  132000U" in config and
            "no_pan_sound_locked();" in engine and
            "sound_play(SOUND_NO_PAN);" in engine and
            "play_table(k_sound_midi_nutcracker_pas_de_deux" in sound and
            "sound_start_count(SOUND_NO_PAN)" in engine and
            "sound_completion_count(SOUND_NO_PAN)" in engine and
            "no_pan_melody_finished_locked(now_us)" in engine and
            "pattern == SOUND_IGBT_WARNING || pattern == SOUND_NO_PAN" in sound and
            "return pattern == SOUND_WAKE || pattern == SOUND_SLEEP ||\n           pattern == SOUND_IGBT_WARNING" in sound and
            "pattern == SOUND_CRITICAL" in sound,
            "NoPan plays one complete 128-second mandatory melody before faulting")
    require(len(nutcracker_notes) == 286 and nutcracker_notes[-1] == (0, 0, 0) and
            sum(on_ms + gap_ms for _, on_ms, gap_ms in nutcracker_notes) == 128000,
            "the public Nutcracker table is complete and exactly 128 seconds")
    require("#define MCL02M_NO_PAN_SAMPLES 3U" in
            (SHARED_POWER / "safety.h").read_text(encoding="utf-8"),
            "NoPan is accepted after three consecutive 500-ms samples")
    require("#define MCL02M_SMALL_COOKWARE_MAX_GEAR 35U" in power_safety and
            "r26 == 0x01 || r26 == 0x02" in power and
            "s_status.cookware_limited = true;" in power and
            "s_status.applied_gear = MCL02M_SMALL_COOKWARE_MAX_GEAR;" in power and
            "if (s_status.applied_gear != 0) s_status.topology = 0xa1;" in power and
            "gear = cookware_limited_gear_locked(gear);" in engine and
            '"SMALL COOKWARE"' in engine and
            "COOKER_SMALL_COOKWARE_NOTICE_MS" in display,
            "R26=01 confirms heating, enforces the stock gear-35/A1 cookware limit, and explains it")
    require("s_status.transition_kind == PB_TRANSITION_START" in power and
            "s_status.transition_command_transmitted" in power and
            "s_status.feedback_sequence > s_transition_feedback_baseline" in power and
            "now_us < s_transition_deadline_us" in power and
            "if (!interrupted && command_transmitted)" in power and
            'case PB_TRANSITION_START: reason = "START TIMEOUT"' in power and
            "fault_locked(reason);" in power and
            "MCL02M_START_CONFIRM_TIMEOUT_MS :" in power and
            "(int64_t)timeout_ms * 1000" in power,
            "Start confirmation opens only after a successful nonzero heartbeat and closes strictly at one eight-second deadline")
    require("powerboard_start_incident_t" in power_header and
            "capture_start_incident_locked" in power and
            'strcmp(reason, "START TIMEOUT")' in power and
            "s_status.start_incident.valid" in power and
            "incident->transmitted_gear = s_last_successful_command_0c" in power and
            "incident->transmitted_topology = s_last_successful_command_0d" in power and
            '"X,%" PRIu32' in power and
            '\\"start_incident\\"' in power and
            "POWERBOARD_STATUS_JSON_MAX 4096" in web,
            "EST preserves one first-cause RAM incident and exposes compact UART plus authenticated status evidence")
    require("powerboard_i2c_incident_t" in power_header and
            "capture_i2c_incident_locked" in power and
            "read_error_mask" in power_header and "write_error_mask" in power_header and
            '\\"i2c_incident\\"' in power and
            '"COMMAND LOSS"' in power and '"CRITICAL LOSS"' in power,
            "E09 preserves first-cause RAM evidence with read/write masks and timing")
    require("#define COOKER_MAX_TIMER_S              (5U * 60U * 60U)" in config,
            "cooking timer is capped at five hours")
    require("COOKER_HOLD_MAX_GEAR" in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8"),
            "PI output is clamped by the HOLD limit")
    require("controller->last_gear == COOKER_HOLD_MAX_GEAR && error >= 3" in
            (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8"),
            "HOLD SATURATED measures actual gear-35 dwell with at least 3 C undershoot")
    require("if (error <= 0)" in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8") and
            "heat_enabled" not in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8") and
            "s_active_zero" in engine and "apply_output_locked(gear)" in engine and
            '"ACTIVE ZERO"' in engine,
            "temperature mode uses active zero at or above target and resumes one degree below")
    require("#define COOKER_TEMP_TREND_WINDOW_MS      4000U" in config and
            "COOKER_TEMP_BRAKE_BASE_C" in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8") and
            "COOKER_TEMP_BRAKE_MAX_C" in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8") and
            "COOKER_TEMP_HIGH_BRAKE_MIN_C" in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8") and
            "approach_exit_error_c" in (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8"),
            "temperature PREHEAT uses a bounded four-second adaptive braking margin with hysteresis")
    require("temperature_ctrl_reset_trend" in engine and
            "readings_were_valid && !s_status.readings_valid" in engine and
            'emit_status("temperature_reading_gap")' in engine and
            "controller->trend_count = 0" in
            (MAIN / "temperature_ctrl.c").read_text(encoding="utf-8"),
            "a sensor-data gap invalidates stale trend evidence without resetting the whole controller")
    require("s_status.state != COOK_STATE_COOKING" in engine and
            "s_status.transition_pending) return;" in engine and
            "if (s_status.timer_remaining_s > 0)" in engine and
            "update_timer_locked" in engine,
            "cooking countdown freezes on Pause, NoPan and pending output transactions")
    require("s_status.state == COOK_STATE_FAULT" in engine and
            "COOK_STATE_SLEEP" not in engine[engine.find("set_fault_locked"):engine.find("map_power_fault")],
            "fault latch cannot auto-enter Sleep")
    require('cooker.state == COOK_STATE_NO_PAN' in display and
            'picture = oled_image_no_pan;' in display and
            '"НЕТ ПОСУДЫ" : "NO PAN"' not in display,
            "NoPan uses the dedicated picture without telemetry or status lines")
    require("#define CRITICAL_BURSTS 3U" in sound and
            "#define CRITICAL_MOTIFS_PER_BURST 2U" in sound and
            "#define CRITICAL_BURST_GAP_MS 4000U" in sound and
            "play_table(k_sound_critical" in sound,
            "critical alarm is three two-motif bursts with four-second gaps")
    require("mandatory_pattern(pattern)" in sound,
            "sound-off setting cannot silence NoPan or the critical alarm")
    require(hashlib.sha256(melodies.encode("utf-8")).hexdigest() ==
                "8c0c5c06a8e21bb77275d61b6b30fb85b546ab97d1c9dc127276433033a70b72" and
            hashlib.sha256(nutcracker.encode("utf-8")).hexdigest() ==
                "7b298f958ef6309b99a3ad6abb2fee66877ad275a0c2bb7c047f71b984e9f0d7" and
            all(name in melodies for name in ("k_sound_boot", "k_sound_complete",
                                              "k_sound_critical", "k_sound_sleep",
                                              "k_sound_wake")) and
            "k_sound_midi_nutcracker_pas_de_deux" in nutcracker,
            "all six approved public PWM melodies are byte-exact")
    require("ui_buzzer_chirp_duty" in output_header and
            "ui_buzzer_chirp_duty" in outputs and
            "SELECTED_SOUND_SLEEP_DUTY_PERMILLE  180U" in melodies and
            "play_table(k_sound_sleep, SELECTED_SOUND_SLEEP_DUTY_PERMILLE" in sound and
            "SELECTED_SOUND_NORMAL_DUTY_PERMILLE 500U" in melodies,
            "table player supports 18-percent sleep duty and 50-percent normal duty")
    wake_branch = engine[engine.find("case INTENT_WAKE"):
                         engine.find("case INTENT_ACK")]
    require("s_status.state == COOK_STATE_SLEEP" in wake_branch and
            "sound_stop();" not in wake_branch and "sound_play(SOUND_WAKE);" in wake_branch and
            "SOUND_WAKE" in sound and "play_table(k_sound_wake" in sound,
            "wake is queued only on a real Sleep-to-Idle transition without cutting Sleep")
    require("sound_cancel(SOUND_NO_PAN);" in engine and
            "cancel_no_pan_sound_locked();\n    powerboard_control_stop" in engine,
            "NoPan is cancelled selectively on pan return or transactional Stop")
    require("protected_pattern" in sound and
            "s_protected_pending" in sound and
            "if (!protect &&" in sound and
            "sound_cancel(sound_pattern_t pattern)" in sound and
            "sound_start_count(sound_pattern_t pattern)" in sound and
            "sound_completion_count(sound_pattern_t pattern)" in sound,
            "long transition melodies discard ordinary queued sounds and support targeted cancellation")
    require("sound_cancel(SOUND_WAKE);\n    s_temperature_edit_deadline_us = 0;" in ui and
            "remember_primary_wake_selection();\n        sound_cancel(SOUND_WAKE);\n        cooking_sleep();" in ui,
            "Cancel and forced long-hold Sleep are the explicit Wake cancellation paths")
    require("MCL02M_PRIVATE_SOUND_BUILD" in project_cmake and
            "mcl02m_custom_private" in project_cmake and
            "private_sound_tables.c" in cmake and
            "melody_tables_midi.generated.h" in cmake and
            "k_sound_midi_lce" in private_sound and
            "k_sound_midi_snm" in private_sound and
            "/assets/sounds/midi/" in gitignore and
            "/firmware/production/build_private/" in gitignore,
            "private Wake/Sleep melodies use an opt-in ignored build flavor")
    if private_midi_path.exists():
        private_midi = private_midi_path.read_text(encoding="utf-8")

        def private_table_duration(name: str) -> int:
            match = re.search(
                rf"static const buzzer_note_t {name}\[\]\s*=\s*\{{(.*?)\n\}};",
                private_midi,
                re.DOTALL,
            )
            if match is None:
                return -1
            notes = re.findall(r"\{(\d+),\s*(\d+),\s*(\d+)\}", match.group(1))
            return sum(int(on_ms) + int(gap_ms)
                       for _, on_ms, gap_ms in notes)

        require(private_table_duration("k_sound_midi_lce") == 8704 and
                private_table_duration("k_sound_midi_snm") == 7460,
                "local private Wake/Sleep tables have the approved exact durations")
    public_map = BUILD / "mcl02m_custom.map"
    if public_map.exists():
        public_symbols = public_map.read_text(encoding="utf-8", errors="replace")
        require("k_sound_midi_lce" not in public_symbols and
                "k_sound_midi_snm" not in public_symbols and
                "private_sound_note" not in public_symbols,
                "public firmware contains no private Wake/Sleep melody symbols")
    require("sound_stop();" in engine[engine.find("case INTENT_ACK"):
                                      engine.find("case INTENT_SCHEDULE_REL")],
            "fault acknowledgement immediately silences a remaining alarm pattern")
    require("/api/control" not in web and "control_handler" not in web,
            "web firmware exposes no heating-control endpoint")
    web_heating_calls = ("cooking_start(", "cooking_stop(", "cooking_pause_resume(",
                         "cooking_set_power(", "cooking_set_temperature(",
                         "cooking_timer_set(", "cooking_schedule_")
    require(not [call for call in web_heating_calls if call in web],
            "web handlers cannot start, stop, pause or reconfigure heating")
    require("remote_arm" not in engine.lower() and "remote_arm" not in ui.lower() and
            "remote_arm" not in web.lower(),
            "obsolete remote-arm state is completely removed")
    central = ui[ui.find("static void central_short"):
                 ui.find("static void central_long")]
    profile_branch = central[central.find("case VIEW_PROFILES"):
                             central.find("case VIEW_PROFILE_READY")]
    require("cooking_start" not in profile_branch,
            "loading a physical-panel preset never starts heating")
    profile_ready = central[central.find("case VIEW_PROFILE_READY"):
                            central.find("case VIEW_WIFI_MENU")]
    require("cooking_start();" in profile_ready and
            "COOKER_PROFILE_STAGE_COUNT       5U" in config and
            "prepare_profile_stage_locked(next, true)" in engine and
            "stage->timer_s == 0" in engine and "sound_play(SOUND_STAGE)" in engine,
            "five timed profile cells skip zero durations and require a second physical Start")
    require("MCL02M_ACTIVE_ZERO_ENABLED=1" in cmake and
            "MCL02M_ACTIVE_ZERO_DIAGNOSTICS=1" in cmake and
            "PB_STATE_ACTIVE_ZERO" in power and "#define PB_ACTIVE_ZERO_0D 0x81U" in power and
            "command_state == PB_STATE_ACTIVE_ZERO" in power and
            "powerboard_control_set_gear(unsigned gear)" in power and
            "if (gear > MCL02M_MAX_GEAR)" in power and
            'normal_stop_locked("GEAR ZERO", false)' not in engine,
            "POWER and temperature gear zero use the diagnostic active-zero session command")
    require("s_status.state == PB_STATE_STOPPED || s_status.state == PB_STATE_ARMED" in power and
            "if (!state_can_energize(s_status.state) && r26_valid && r26 != 0)" not in power and
            "Preserve the first cause" in power,
            "active-zero and Pause retain the session without a false STOP VERIFY fault")
    require("PB_STATE_STOPPING" in power_header and
            "COOK_STATE_STOPPING" in source and
            "#define MCL02M_STOP_CONFIRM_TIMEOUT_MS 8000U" in power_safety and
            "#define MCL02M_STOP_CONFIRM_SAMPLES 2U" in power_safety and
            "begin_stop_locked" in power and "finish_stop_locked" in power and
            "freeze_stop_evidence_locked" in power and
            "s_stop_zero_samples < MCL02M_STOP_CONFIRM_SAMPLES" in power and
            'record_stop_issue_locked("I2C LOST")' in power and
            'record_stop_issue_locked("STOP TIMEOUT")' in power and
            "begin_normal_stop_locked" in engine and
            "finish_normal_stop_locked" in engine and
            "pb->state == PB_STATE_STOPPED && pb->stop_verified" in engine and
            "s_status.state != PB_STATE_FAULT || !s_status.stop_verified" in power and
            'begin_normal_stop_locked("PROFILE COMPLETE", true)' in engine and
            'begin_normal_stop_locked("TIMER COMPLETE", true)' in engine and
            'begin_normal_stop_locked("PAUSE TIMEOUT", false)' in engine and
            "class StopTransaction" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8"),
            "all normal and safety Stop origins use one idempotent transaction confirmed by two fresh R26=00 samples")
    require("MCL02M_COOKING_LEASE_ENABLED=1" in cmake and
            "#define MCL02M_COOKING_LEASE_MS 3000U" in power_safety and
            "powerboard_control_lease_begin" in power_header and
            "powerboard_control_lease_renew" in power_header and
            "expire_lease_locked" in power and
            'begin_stop_locked("COOK LEASE")' in power and
            "now_us >= s_lease_deadline_us" in power and
            "generation != s_status.lease_generation" in power and
            "!lease_ready || preflight_issue != NULL" in power and
            'ESP_LOGE(TAG, "L,EXPIRE,%" PRIu32' in power and
            "powerboard_control_lease_begin(&s_lease_generation)" in engine and
            "powerboard_control_lease_renew(s_lease_generation)" in engine and
            "!state_active(s_status.state) || s_waiting_pan_before_start" in engine and
            "renew_cooking_lease_locked();" in engine and
            "pb->state == PB_STATE_STOPPED && state_active(s_status.state)" in engine and
            "if (pb->lease_expired)" in engine and
            "FAULT_COOKING_LEASE" in source and 'code = "ECL"' in display and
            "esp_task_wdt_add(NULL)" in power and
            "class CookingLease" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8"),
            "the generation-tagged three-second cooking lease is renewed only by the cooking task and independently expires into transactional Stop")
    require("powerboard_transition_t" in power_header and
            "powerboard_feedback_state_t" in power_header and
            "#define MCL02M_TRANSITION_CONFIRM_TIMEOUT_MS 3000U" in power_safety and
            "begin_transition_locked" in power and
            "finish_transition_locked" in power and
            "s_status.transition_generation == transition_generation" in power and
            "s_transition_feedback_baseline = s_status.feedback_sequence" in power and
            "s_status.feedback_sequence > s_transition_feedback_baseline" in power and
            "transition_r20_compatible" in power and
            "r26 == 0x01 || r26 == 0x02" in power and
            "command_0d == PB_ACTIVE_ZERO_0D && command_00 == 0" in power and
            "command_0d == topology_for_gear(transition_requested_gear)" in power and
            "s_status.transmitted_gear = command_0c" in power and
            "s_status.confirmation_inferred = true" in power and
            "s_status.feedback_gear_known = false" in power and
            "reject_transition_locked" in power and
            '"T,REJECT,%" PRIu32' in power and
            "char transition_rejection[32]" in power_header and
            "preflight_issue_locked" in power and
            "retained_session_issue_locked" in power and
            'snprintf(reason, sizeof(reason), "START %s"' in power and
            'snprintf(reason, sizeof(reason), "RESUME %s"' in power and
            "retained_resume_first_gear" in power and
            "return target;" in
            power[power.find("static uint8_t retained_resume_first_gear"):
                  power.find("static void advance_ramp")] and
            "copy_transition_status_locked" in engine and
            "apply_confirmed_transition_locked" in engine and
            "Repeated short presses cannot invert an unconfirmed transition" in engine and
            "if (status.transition_pending) return;" in ui and
            '"PAUSE PENDING"' in engine and '"RESUME PENDING"' in engine and
            "class ConfirmedTransition" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8"),
            "Start, active zero, Pause and Resume use generation-tagged transmitted-command plus fresh-feedback confirmation without claiming a reported gear")
    require("PB_TRANSITION_PAN_RETURN_HOLD" in power_header and
            "PB_TRANSITION_PAN_RETURN_RESUME" in power_header and
            "r20_proves_pan_present" in power and
            "begin_transition_locked(PB_TRANSITION_PAN_RETURN_HOLD" in power and
            "powerboard_control_pan_return_resume" in power and
            "s_status.state = PB_STATE_NO_PAN" in power and
            "s_status.applied_gear = 0" in power and
            "request_pan_return_output_locked" in engine and
            "reset_temperature_after_interruption_locked(true)" in engine and
            'emit_status("pan_return_safe_hold_confirmed")' in engine and
            'emit_status("pan_return_resume_requested")' in engine and
            "const bool pan_return_transition" in engine and
            "replace_pan_return_with_pause" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8"),
            "NoPan return first confirms active zero, then recomputes and confirms fresh output; Stop/Pause generations cannot be undone by stale feedback")
    require("status_buffers_t *buffers = calloc" in web and
            "free(buffers);" in web and
            "component json overflow" in web and
            "#if !MCL02M_COMPACT_UART_TELEMETRY" in power and
            "static char json[4096]" in power,
            "expanded transition diagnostics avoid the power and HTTP task stacks")
    require("retained_session_issue_locked" in power and
            'return "R26 OUTPUT OFF"' in power and
            "retained_issue != NULL" in power and
            'ESP_LOGW(TAG, "Z,REJECT,PAUSE' in power and
            'ESP_LOGI(TAG, "C,RESUME,%d"' in engine and
            'ESP_LOGI(TAG, "B,U,%lld"' in inputs,
            "Pause resumes only from a healthy retained session and logs every decision")
    pause_branch = engine[engine.find("case INTENT_PAUSE_RESUME"):
                          engine.find("case INTENT_SLEEP")]
    require("temperature_ctrl_restart(&s_temperature);" in pause_branch and
            "temperature_ctrl_update(" in pause_branch and
            "apply_output_locked(resume_gear)" in pause_branch and
            pause_branch.find("apply_output_locked(resume_gear)") <
            pause_branch.find("powerboard_control_resume()") and
            "s_last_temp_update_us = now_us;" in engine and
            'emit_status("manual_resume_confirmed")' in engine and
            "temperature_ctrl_observe(&s_temperature" in engine,
            "temperature Pause keeps trend observations and precomputes a fresh output before Resume")
    require("MCL02M_COMPACT_UART_TELEMETRY=1" in cmake and
            "#if !MCL02M_COMPACT_UART_TELEMETRY" in telemetry and
            '"D,%s,%u,%u,%02X,%02X,%02X,%02X,"' in power and
            'ESP_LOGW(TAG, "F,%s,%02X,%02X,%02X,%02X,%02X,%s"' in power and
            'ESP_LOGW(TAG, "I,R,%02X,%d"' in power and
            "s.registers[5]" in power and "s.active_zero_resumes" in power and
            "s.arm_remaining_ms" in power and "s.start_confirm_remaining_ms" in power and
            "s.heartbeat_gap_remaining_ms" in power and
            "s.heartbeat_gap_observed_stop" in power,
            "UART diagnostics use complete compact frames instead of periodic JSON")
    require('"B,%04X,%04X,%u,' in power and
            '"C,%s,%s,%u,%u,%u,%u,%u,%" PRIu32' in engine and
            "startup_required_valid_mask" in power_header and
            "startup_service_valid_mask" in power_header and
            "return required_result;" in power,
            "boot capabilities and cooking events have compact UART evidence while R2C-R2F stay best-effort")
    require("#define COOKER_MANUAL_PAUSE_TIMEOUT_MS   (2U * 60U * 60U * 1000U)" in config and
            "update_manual_pause_timeout_locked" in engine and
            'begin_normal_stop_locked("PAUSE TIMEOUT", false)' in engine and
            "s_status.state != COOK_STATE_PAUSED" in engine and
            "powerboard_control_pause()" in engine and "PB_STATE_PAUSED" in power,
            "manual Pause uses active zero and performs a full Stop after two hours")
    require("stage->gear > COOKER_MAX_GEAR" in settings and
            "stage->mode == COOK_MODE_POWER && stage->gear == 0" not in settings and
            web.count("min=0 max=99") == 5 and "gear < 0 || gear > 99" in web,
            "timed POWER profile stages accept gear zero as a wait stage")
    require('\\"cmd_0d\\":%u' in power and '\\"active_zero_entries\\":%' in power and
            '\\"active_zero_enabled\\":%s' in power and '\\"active_zero\\":%s' in engine and
            "powerboard_control_status_json" in web,
            "UART and authenticated Wi-Fi status expose removable active-zero diagnostics")
    require("state_schedulable" in engine and
            "if (!state_schedulable(snapshot.state)) return ESP_ERR_INVALID_STATE" in engine,
            "delayed Start cannot overwrite an active cooking state")
    require("#define COOKER_RETAINED_SESSION_LIMIT_MS (8U * 60U * 60U * 1000U)" in config and
            "MCL02M_MAX_RUN_MS=28800000U" in cmake and
            "powerboard_control_start(gear, COOKER_RETAINED_SESSION_LIMIT_MS)" in engine and
            "update_session_time_buckets_locked" in engine and
            "retained_session_remaining_s" in app_types and
            "heating_elapsed_s" in app_types and
            "active_zero_elapsed_s" in app_types and
            "profile_zero_wait_elapsed_s" in app_types and
            "manual_pause_elapsed_s" in app_types and
            "no_pan_elapsed_s" in app_types and
            "session_remaining_s" in engine and "profile_zero_s" in engine and
            "class SessionTimeBuckets" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8"),
            "five-hour countdown, eight-hour retained-session wall bound, heating, active-zero, profile-zero, Pause, NoPan and lease time remain separate")
    require("INTENT_SET_MODE" not in engine and "INTENT_SET_POWER" not in engine and
            "INTENT_SET_TEMP" not in engine and
            "INTENT_START" in engine and
            "static esp_err_t set_mode_locked" in engine and
            "static esp_err_t set_power_locked" in engine and
            "static esp_err_t set_temperature_locked" in engine and
            "xSemaphoreTake(s_lock, portMAX_DELAY);" in
            engine[engine.find("esp_err_t cooking_set_mode"):
                   engine.find("esp_err_t cooking_profile_select")],
            "return post(INTENT_START, 0, NULL);" in engine and
            "mode, power and temperature changes complete before queued Start")
    require("static bool state_configurable" in engine and
            "state == COOK_STATE_READY || state == COOK_STATE_COMPLETE" in engine and
            "if (!state_configurable(s_status.state)) return ESP_ERR_INVALID_STATE;" in engine and
            "!state_configurable(s_status.state)" in engine and
            "cooking_set_mode(COOK_MODE_POWER) == ESP_OK" in ui and
            "cooking_set_mode(COOK_MODE_TEMPERATURE) == ESP_OK" in ui,
            "mode and profile changes cannot destroy an active delayed Start")
    require("bool armed = false;" in engine and
            "if (armed) powerboard_control_stop(\"START ROLLBACK\");" in engine,
            "a failed Start after Arm explicitly rolls the power board back to Stop")
    central_short = ui[ui.find("static void central_short"):
                       ui.find("static bool central_long")]
    central_long = ui[ui.find("static bool central_long"):
                      ui.find("static void cancel_action")]
    cancel_action = ui[ui.find("static void cancel_action"):
                       ui.find("static void input_event")]
    lower_pause = power[power.find("esp_err_t powerboard_control_pause"):
                        power.find("esp_err_t powerboard_control_pan_return_resume")]
    require('cooking_stop("NO PAN CENTER");' in central_short and
            central_short.find("status.state == COOK_STATE_NO_PAN") <
                central_short.find("if (status.transition_pending) return;") and
            "status.state == COOK_STATE_NO_PAN" in central_long and
            'cooking_stop("CENTER HOLD");' in central_long and
            "status.state == COOK_STATE_NO_PAN" in cancel_action and
            'cooking_stop("CANCEL");' in cancel_action and
            "if (s_status.state == COOK_STATE_NO_PAN)" in pause_branch and
            'begin_normal_stop_locked("NO PAN INPUT", false);' in pause_branch and
            "s_status.state == PB_STATE_NO_PAN" in lower_pause and
            'reject_transition_locked("PAUSE NO PAN");' in lower_pause,
            "NoPan center short/long and Cancel converge on idempotent Stop; Pause cannot arm an EPB-producing transition")
    require("s_status.state != COOK_STATE_STARTING &&" in engine and
            "s_status.state != COOK_STATE_COOKING" in engine and
            "const bool start_update = s_status.state == COOK_STATE_STARTING" in engine and
            "begin_transition_locked(PB_TRANSITION_START" in power and
            'ESP_LOGI(TAG, "T,RESTART,%u,%u"' in power and
            "temperature_target_safe_zero" in engine and
            "set_fault_locked(FAULT_POWER_STATUS, \"TEMP UPDATE FAILED\")" in engine,
            "temperature changes during Start apply immediately or force safe zero/fault")
    require("ui_direct_output_set(PIN_UI_DIRECT_1, 0)" in indicators,
            "unidentified GPIO32 remains LOW")
    require("self_test_deadline" in indicators and "leds = 9;" in indicators,
            "all nine white LEDs run a visible boot self-test")
    require("static void led_end_command(void)" in outputs and
            "led_begin_command(0x8f);" in outputs and
            "led_end_command();" in outputs and
            'ESP_LOGI(TAG, "L,%u,%u,%u,%u,%d,%d"' in indicators,
            "serial LED transactions latch the final command and changed outputs are logged compactly")
    require("cooker.state == COOK_STATE_FAULT" in display and
            "urgent_screen && s_timer_editing" in ui and
            "if (urgent_screen) s_temperature_edit_deadline_us = 0;" in ui and
            "if (live && !s_timer_editing && !temperature_editing)" in ui,
            "fault/live screens cannot be hidden behind a menu overlay")
    require("TIMER_SCREEN_ALWAYS" in display and "ui_oled_show_timer" in display,
            "TIMER SCREEN setting supports movable countdown or full OLED off")
    require("#define SETTING_ITEMS 12" in ui and
            ui.count('"SHOW"') >= 4 and ui.count('"ПОКАЗАТЬ"') >= 4 and
            all(label in ui for label in ('"LIVE DATA"', '"IGBT T°C"',
                                          '"TIMER SCREEN"', '"SLEEP CLOCK"')),
            "Settings 3-6 use SHOW plus a descriptive second line")
    require("#define SETTING_FIRMWARE_VERSION_INDEX 10U" in ui and
            "#define SETTING_FACTORY_INDEX 11U" in ui and
            "VIEW_FIRMWARE_VERSION" in ui and
            "display_prod_set_version_overlay" in ui and
            "MCL02M_FIRMWARE_VERSION" in ui and
            '"PWR BOARD"' in ui and
            'snprintf(l0, sizeof(l0), "R28 %02X"' in ui and
            "OVERLAY_VERSION" in display and
            "ui_oled_show_version(overlay_a, overlay_b, overlay_c, overlay_d)" in display and
            "oled_draw_scaled_text(firmware_title, 0, 5, 1, 1);" in outputs and
            "oled_draw_scaled_text(board_revision, 0, 37, 1, 1);" in outputs,
            "physical Settings shows left-aligned firmware and live R28 power-board revision")
    require("r20_silent_nonfault" in power and
            "value == 0x2b" in power and "value == 0x29" in power and
            "value == 0x2a" in power and
            "!r20_silent_nonfault(r20)" in power and
            "s_status.unknown_r20_seq" in power and
            'tr(lang, "WARNING", "ВНИМАНИЕ", "警告")' in display and
            'tr(lang, "UNKNOWN", "НЕИЗВЕСТНО", "未知状态")' in display and
            'tr(lang, "PRESS", "НАЖМИТЕ", "按任意键")' in display and
            'tr(lang, "ANY KEY", "ЛЮБ КНОПКУ", "")' in display and
            "r20," in display and
            "cooking_acknowledge_warning();" in ui and
            "input_status.r20_warning_active && !true_urgent" in ui and
            'fault_locked("POWER TRANSITION")' not in power,
            "unknown R20 is dismissible warning while 2B/29/2A stay silent and nonfatal")
    require("#define COOKER_I2C_DEBUG_DISPLAY_ENABLED  0U" in config and
            "#if COOKER_I2C_DEBUG_DISPLAY_ENABLED" in display and
            "#if !COOKER_I2C_DEBUG_DISPLAY_ENABLED" in settings and
            "show_i2c_debug" in settings and
            "s_settings.show_i2c_debug = 0;" in settings and
            "if (stored.schema <= 4U) s_settings.show_i2c_debug = 0;" in settings and
            'case SETTING_I2C_DEBUG_INDEX: return "I2C ERRORS";' in ui and
            "case SETTING_I2C_DEBUG_INDEX: s_setting_value = settings.show_i2c_debug;" in ui and
            "case SETTING_I2C_DEBUG_INDEX: settings.show_i2c_debug = s_setting_value;" in ui,
            "temporary I2C counter display remains in source but is compiled out of production")
    require("pb->consecutive_bad_cycles > COOKER_I2C_DEBUG_MAX" in engine and
            "s_status.i2c_bad_cycles" in engine and
            "i2c_debug_display_value(cooker.i2c_bad_cycles, now)" in display and
            "COOKER_I2C_DEBUG_HOLD_MS" in display and
            "#define COOKER_I2C_DEBUG_HOLD_MS       2000U" in config and
            "oled_draw_scaled_text(text, 0, 10, 1, 1)" in outputs,
            "I2C debug counter resets internally but holds its displayed peak for two seconds")
    require("s_temperature_edit_value" in ui and
            "display_prod_set_temperature_edit_overlay(s_temperature_edit_value" in ui and
            "cooking_set_temperature(s_temperature_edit_value);" in ui,
            "temperature editor owns a clamped immediate setpoint instead of showing an asynchronous stale value")
    cjk_codepoints = {
        ord(char) for char in ui + display if 0x3400 <= ord(char) <= 0x9FFF
    }
    cjk_font = {
        int(value, 16)
        for value in re.findall(r"\{0x([0-9A-Fa-f]{4}), \{", outputs)
    }
    require("LANG_ZH = 2" in (MAIN / "app_types.h").read_text(encoding="utf-8") and
            "settings->language > LANG_ZH" in settings and
            '<option value=2>简体中文</option>' in web and
            "(bytes[0] & 0xf0) == 0xe0" in outputs and
            not (cjk_codepoints - cjk_font) and
            all(label in ui for label in ('"功率"', '"温度"', '"预设"',
                                          '"信息"', '"启动"', '"设置"', '"时钟"')),
            "Simplified Chinese is a persisted third language with complete compact OLED glyph coverage")
    require("ui_controller_timer_editing()" in indicators and
            "s_timer_editing = true" in ui,
            "timer LED is on immediately while the three-stage editor is open")
    require("return event->value < 0 ? magnitude : -magnitude;" in ui,
            "clockwise encoder rotation increases menu and setpoint values")
    require("#define HOME_ITEMS 7" in ui and
            all(label in ui for label in ('"POWER"', '"T°C"', '"PRESET"',
                                          '"INFO"', '"START"', '"SETUP"', '"CLOCK"')) and
            '"DELAYED"' in ui and '"WI-FI"' in ui and
            '"TIMER"' not in ui[ui.find("static const char *home_name"):
                                  ui.find("static const char *setting_name")],
            "main menu adds Clock while Wi-Fi stays inside Setup and Timer remains button-only")
    require("#define EDITOR_TIMEOUT_US (10LL * 1000000LL)" in ui and
            "if (s_timer_editing) close_timer_editor();" in ui and
            "else open_timer_action();" in ui,
            "timer action opens from the left button and cancels or times out")
    require("#define BLINK_VISIBLE_MS 700U" in ui and "< 700U" in indicators,
            "editable values and readiness LEDs use a 70/30 visible blink")
    require("s_status.selected_gear = 1;" in engine and
            'display_prod_set_focus_overlay("", right, l1, false, "")' in ui and
            "ui_oled_show_focus" in display,
            "POWER defaults to gear 1 and uses the uncluttered large-number screen")
    require("#define TEMPERATURE_EDIT_TIMEOUT_US (2LL * 1000000LL)" in ui and
            "display_prod_set_temperature_edit_overlay" in ui and
            "OVERLAY_TEMPERATURE_EDIT" in display and
            "ui_oled_show_temperature_editor" in display and
            'snprintf(selected, sizeof(selected), "S%u°"' in outputs and
            "oled_draw_centered_fit(selected, 5, 2, 2)" in outputs and
            "oled_draw_centered_fit(current, 29, 2, 2)" in outputs,
            "TEMPERATURE editing shows set/current as two 2x rows")
    require("s_temperature_edit_deadline_us = esp_timer_get_time() +" in ui and
            "now >= s_temperature_edit_deadline_us" in ui and
            "live && !s_timer_editing && !temperature_editing" in ui,
            "live or paused TEMPERATURE editing temporarily overrides the normal/timer screen for two seconds")
    require("#define UI_MAIN_LONG_PRESS_MS 1500U" in inputs and
            "button_long_posted" in inputs and
            ">= UI_MAIN_LONG_PRESS_MS * 1000LL" in inputs and
            "event->type == UI_INPUT_MAIN_LONG" in ui and "s_swallow_main = true" in ui and
            "if (s_swallow_main) return;" in ui,
            "center long-hold fires once at 1.5 seconds and preserves the OLED wake guard")
    require("event->type == UI_INPUT_TOUCH_A_PRESSED || event->type == UI_INPUT_TOUCH_BOTH_PRESSED" in ui and
            "event->type == UI_INPUT_TOUCH_B_PRESSED" in ui and
            "cancel_action();" in ui and "sound_play(SOUND_UI_CLICK);" in ui,
            "physical Cancel/Timer mapping is correct and Cancel has a click")
    require("display_prod_set_info_overlay" in ui and "ui_oled_show_info" in display and
            all(label in outputs for label in ('"VOLT"', '"NTC"', '"IGBT"')) and
            "oled_draw_info_line(0" in outputs and "oled_draw_info_line(16" in outputs and
            "oled_draw_info_line(32" in outputs,
            "INFO uses three equally-sized, spaced label/value rows")
    require("ui_controller_setpoint_editing()" in indicators and
            "cooker.state == COOK_STATE_READY" in indicators,
            "readiness LEDs stop blinking after leaving POWER/TEMPERATURE editing")
    require("VIEW_TIMER_DISABLE" in ui and "cooking_timer_disable()" in ui and
            "INTENT_TIMER_TOGGLE" not in engine and
            "s_status.timer_enabled = false;" in engine and
            "if (s_timer_editing) close_timer_editor();" in ui,
            "an active timer uses synchronous explicit disable; Timer backs out unchanged")
    require("VIEW_TIMER_SECONDS" in ui and "VIEW_TIMER_MINUTES" in ui and
            "s_view = VIEW_TIMER_MINUTES;" in ui and
            "s_view = VIEW_TIMER_HOURS;" in ui and
            "s_timer_minutes * 60U + s_timer_seconds" in ui and
            'tr(lang, "SECONDS", "СЕКУНДЫ", "秒")' in ui and
            'tr(lang, "MINUTES", "МИНУТЫ", "分钟")' in ui,
            "timer editor confirms seconds, then minutes, then hours")
    timer_api = engine[engine.find("esp_err_t cooking_timer_set"):
                       engine.find("esp_err_t cooking_schedule_relative")]
    require("xSemaphoreTake(s_lock, portMAX_DELAY);" in timer_api and
            "s_status.timer_enabled = true;" in timer_api and
            "s_status.timer_enabled = false;" in timer_api and
            "post(" not in timer_api,
            "timer Set and Disable complete synchronously without queued toggle races")
    require("status.state == COOK_STATE_DELAYED" in ui and "cooking_schedule_cancel();" in ui and
            "VIEW_START_IN_MINUTES" in ui and "VIEW_START_IN_HOURS" in ui,
            "center hold cancels delayed Start and START IN edits minutes then hours")
    require("static bool update_schedule_locked" in engine and
            "if (update_schedule_locked(now))" in engine and
            "powerboard_control_get_status(&pb);" in
            engine[engine.find("if (update_schedule_locked(now))"):
                   engine.find("apply_power_status_locked(&pb, now);")],
            "delayed expiry refreshes the stale stopped power-board snapshot before classification")
    require("const bool start_pending = s_status.state == COOK_STATE_STARTING" in engine and
            "s_status.transition_kind == PB_TRANSITION_START" in engine and
            "!start_pending" in engine,
            "pending Start feedback cannot be misclassified as an unexpected Stop")
    long_branch = ui[ui.find("static bool central_long"):
                     ui.find("static void cancel_action")]
    require("synchronize_delayed_start_view" in ui and
            "s_last_cooking_state == COOK_STATE_DELAYED" in ui and
            "s_view = VIEW_POWER" in ui and "s_view = VIEW_TEMPERATURE" in ui and
            "s_view = VIEW_PROFILE_READY" in ui and
            "status.state == COOK_STATE_DELAYED" in long_branch and
            "cooking_stop(\"CENTER HOLD\")" in long_branch and
            long_branch.find("status.state == COOK_STATE_DELAYED") <
            long_branch.find("else if (s_timer_editing)") and
            long_branch.find("cooking_stop(\"CENTER HOLD\")") <
            long_branch.find("else if (s_timer_editing)") and
            long_branch.count("s_timer_editing = false;") >= 3 and
            "status.state == COOK_STATE_DELAYED" in
            ui[ui.find("static void encoder_event"):ui.find("static void central_short")] and
            "def delayed_deadline" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8") and
            "def delayed_after_restart" in
            (ROOT / "tests" / "policy_tests.py").read_text(encoding="utf-8"),
            "delay expiry synchronizes the real mode view, profile browsing cannot mutate a delayed run, and long-center Stop/Cancel outranks timer editors")
    require("ui_oled_show_cooking" in display and "settings.show_context_value" in display and
            "settings.show_igbt" in display and "cooker.timer_enabled" in display,
            "active screens expose timer and optional contextual/IGBT readings")
    encoder_body = ui[ui.find("static void encoder_event"):ui.find("static void central_short")]
    require("sound_play" not in encoder_body and
            "((now / 1000000LL) % 7LL) >= 5LL" in display and
            "settings.show_igbt && !timer_active" in display,
            "encoder stays silent and IGBT replaces context only for the agreed 5s/2s no-timer cycle")
    require("Keep OLED blank until ui_controller" in display and
            "s_overlay_kind = OVERLAY_TEXT" in display and "s_overlay = true" in display,
            "boot cannot expose the transient technical status screen")
    require("oled_draw_centered_fit(subtitle, 14" in outputs and
            "const int area_top = 21" in outputs and "oled_debug_frame" not in outputs,
            "revised number/subtitle/label menu geometry is active without the debug frame")
    require("ui_oled_show_timer" in display and
            "oled_draw_duration(timer, 34, timer_seconds)" in outputs and
            "format_timer_compact" in display,
            "running timer uses a full-width 2x row pinned to the physical bottom edge")
    require("if (paused)" in display and "const bool timer_active = cooker.timer_enabled && !paused" in display and
            "cooker.state == COOK_STATE_COOKING" in display,
            "Pause hides timer and context, including the timer-only timeout screen")
    prime_renderer = outputs[outputs.find("static void oled_draw_prime"):
                             outputs.find("static void oled_draw_duration")]
    require("oled_set_pixel(x, y, true)" in prime_renderer and
            "oled_set_pixel(x + 1, y, true)" not in prime_renderer,
            "minute/second prime marks are straight vertical strokes")
    require("#define UI_OLED_TEXT_LINES 5" in
            (SHARED_UI / "ui_outputs.h").read_text(encoding="utf-8") and
            "(int)row * 10" in outputs and "4U" not in
            outputs[outputs.find("static void oled_make_large_number"):
                    outputs.find("static void outputs_watchdog_task")],
            "OLED uses at most five 7-pixel rows with 3-pixel gaps and no 4x text")
    require("oled_draw_edge_groups(top_left, top_right, 0)" in outputs and
            "const int y = 10 + (21 - height) / 2" in outputs and
            "oled_draw_duration(timer, 34" in outputs,
            "active OLED geometry is pinned to y=0, y=10..30 and y=34..47")
    require('"СТАРТ ЧЕРЕЗ"' in ui and '"СТАРТ В"' in ui and
            "ui_oled_show_time_editor" in display,
            "Timer, Start In/At and Clock use dedicated large time editors")
    require("VIEW_CLOCK_HOURS" in ui and "VIEW_CLOCK_MINUTES" in ui and
            "settimeofday(&wall, NULL)" in ui and
            "s_clock_hour" in ui and "s_clock_minute" in ui,
            "manual Clock is 24-hour HH:MM and remains RAM/runtime-only")
    require("s_selection == 6" in ui and '"%02d:%02d"' in ui and
            'strlcpy(l0, "--:--"' in ui,
            "Clock menu item shows the running HH:MM wall clock")
    require("esp_sntp_set_time_sync_notification_cb(time_sync_notification);" in network and
            "s_status.clock_synchronized = true;" in network and
            "status->clock_synchronized = time(NULL)" not in network and
            "if (network.clock_synchronized) return true;" in ui,
            "real SNTP synchronization is authoritative over manual Clock edits")
    require("strcmp(profiles[s_profile].name, default_name) != 0" in ui,
            "default PROFILE name is not rendered twice")
    require("#define COOKER_DEFAULT_OLED_TIMEOUT_S    180U" in config and
            "18000" in settings and "oled_timeout_valid" in settings and
            "s_settings.show_igbt = 0;" in settings,
            "OLED defaults to three minutes with the agreed steps up to five hours; IGBT defaults off")
    require("show_sleep_clock" in settings and
            "stored.schema == 2U" in settings and
            "s_settings.show_sleep_clock = 1;" in settings and
            "settings.show_sleep_clock && wall > 1700000000" in display and
            "elapsed % 42U" in display and "(elapsed / 42U) % 3U" in display and
            "ui_oled_show_sleep_clock" in outputs,
            "sleep Clock is persisted and moves down one pixel per minute across center/left/right passes")
    require("#define COOKER_SETTINGS_SCHEMA           5U" in config and
            "sizeof(app_settings_t) == 32" in settings and
            "stored.schema == 4U" in settings,
            "settings schema 5 preserves and migrates the 32-byte settings blob")
    require("input_status.state == COOK_STATE_SLEEP && event->type == UI_INPUT_MAIN_PRESSED" in ui and
            "input_status.state == COOK_STATE_SLEEP && event->type == UI_INPUT_ENCODER" in ui and
            "s_swallow_main = true" in ui and "s_encoder_guard_until_us" in ui,
            "center and encoder wake safely even while the sleep Clock keeps OLED powered")
    require("remember_primary_wake_selection();" in ui and
            "restore_primary_wake_selection();" in ui and
            "s_wake_selection" in ui,
            "Sleep wake returns to Power or preserves Temperature, never a secondary menu item")
    image_symbols = (
        "oled_image_cancel", "oled_image_confirm", "oled_image_cooking",
        "oled_image_error", "oled_image_no_pan", "oled_image_ready_1",
        "oled_image_ready_2", "oled_image_ready_3",
        "oled_image_sleep_warning", "oled_image_sleep", "oled_image_turn_on",
        "oled_image_wakeup", "oled_image_wifi_present", "oled_image_hot",
        "oled_image_delayed_start", "oled_image_small_cookware",
        "oled_image_noopls", "oled_image_too_hot", "oled_image_what_is_going_on",
    )
    image_lengths = []
    for symbol in image_symbols:
        match = re.search(
            rf"const uint8_t {symbol}\[OLED_ASSET_FRAME_BYTES\] = \{{(.*?)\}};",
            assets,
            re.DOTALL,
        )
        image_lengths.append(len(re.findall(r"0x[0-9a-fA-F]{2}", match.group(1))) if match else 0)
    require("#define OLED_ASSET_FRAME_BYTES 384U" in asset_header and
            image_lengths == [384] * 19 and
            "OLED_ASSET_FRAME_BYTES == UI_OLED_BITMAP_BYTES" in display and
            "ui_oled_show_bitmap" in outputs and "ui_oled_show_bitmap_text" in outputs and
            "ui_oled_show_bitmap_right_text" in outputs,
            "all 19 approved 64x48 pictures compile as exact 384-byte OLED frames")
    require("error.png" in asset_generator and "for y in range(30, 48)" in asset_generator and
            "for x in range(30, 64)" in asset_generator and
            all(code in display for code in ('code = "E02"', 'code = "E03"',
                                             'code = "E04"', 'code = "E05"',
                                             'code = "E07"', 'code = "E08"',
                                             'code = "E09"', 'code = "E10"',
                                             'code = "E12"', 'code = "EPB"',
                                             'code = "EST"', 'code = "ETM"')) and
            "ui_oled_show_bitmap_text(picture, fault_code, 30, 32, 2)" in display and
            "oled_draw_scaled_text(text, x, y, scale, scale)" in outputs,
            "error artwork reserves its lower-right corner and renders the live code at 2x")
    require("#define COOKER_IMAGE_CONFIRM_MS         1500U" in config and
            "#define COOKER_IMAGE_WAKEUP_MS          3000U" in config and
            "#define COOKER_IMAGE_TURN_ON_MS         5000U" in config and
            "#define COOKER_IMAGE_COOKING_MS         2500U" in config and
            "#define COOKER_IMAGE_CANCEL_MS          1500U" in config and
            "#define COOKER_IMAGE_SLEEP_WARNING_MS  10000U" in config and
            "#define COOKER_IMAGE_SLEEP_MS          10000U" in config,
            "picture durations include a five-second turn-on frame for the 4.8-second melody")
    require(app_main.find("display_prod_init()") < app_main.find("sound_play(SOUND_BOOT);") <
            app_main.find("ui_controller_init()"),
            "turn-on melody starts alongside the five-second turn-on picture")
    require("TRANSIENT_TURN_ON" in display and "TRANSIENT_WAKEUP" in display and
            "TRANSIENT_COOKING" in display and
            "previous == COOK_STATE_SLEEP" in display and
            "active_picture_state(cooker.state) && !active_picture_state(previous)" in display,
            "turn-on, wake-up, Start, Resume and pan-return transitions select their pictures")
    require("TRANSIENT_WIFI_PRESENT" in display and
            "display_prod_show_wifi_present();" in network and
            "network.sta_connected ? \"WI-FI OK\" : \"WI-FI\"" in ui and
            "if (network.sta_connected) display_prod_show_wifi_present();" in ui,
            "successful Wi-Fi connection and entry to a connected Wi-Fi menu show the new artwork")
    require("display_prod_show_confirm();" in ui and
            "display_prod_show_cancel();" in ui and
            "TRANSIENT_CONFIRM" in display and "TRANSIENT_CANCEL" in display,
            "settings confirmation and physical Cancel select their timed pictures")
    input_body = ui[ui.find("static void input_event"):
                    ui.find("static void ui_task")]
    require("void display_prod_dismiss_transient(void)" in display and
            "display_prod_dismiss_transient();" in input_body and
            "s_cookware_notice_deadline_us = 0;" in display and
            "event->type == UI_INPUT_ENCODER" in input_body and
            "event->type == UI_INPUT_MAIN_PRESSED" in input_body and
            "event->type == UI_INPUT_TOUCH_A_PRESSED" in input_body and
            "event->type == UI_INPUT_TOUCH_B_PRESSED" in input_body and
            input_body.find("display_prod_dismiss_transient();") <
            input_body.find("cancel_action();"),
            "physical input dismisses existing transient and small-cookware pictures before its own action")
    active_focus = display[display.find("const bool active_focus"):
                           display.find("if (!effective_overlay", display.find("const bool active_focus"))]
    require("cooker.state == COOK_STATE_STOPPING" in active_focus,
            "transactional Stop retains the clean live focus screen instead of technical STOPPING text")
    require("ready_bitmap(s_ready_image)" in display and
            "s_next_ready_image = (s_next_ready_image + 1U) % 3U" in display and
            "picture = oled_image_error;" in display and
            "picture = oled_image_no_pan;" in display and
            "COOKER_COMPLETE_NOTICE_MS      60000U" in config and
            "update_complete_notice_locked(now);" in engine and
            's_status.state = COOK_STATE_IDLE;' in
                engine[engine.find("static void update_complete_notice_locked"):
                       engine.find("static esp_err_t apply_output_locked")],
            "three alternating Ready pictures stay latched for one minute before Idle resumes Hot/OLED/Sleep policy")
    require("picture = oled_image_delayed_start;" in display and
            "format_timer_compact(cooker.delayed_remaining_s, delay);" in display and
            'snprintf(output, 12, "P%u", s->selected_gear);' in display and
            'snprintf(output, 12, "t%u", s->target_temperature_c);' in display and
            'snprintf(output, 12, "pr%u"' in display,
            "delayed start artwork carries a compact countdown and P/t/pr mode badge")
    require("oled_image_small_cookware, \"P<36\", 1" in display and
            "case '<': return less_than;" in outputs and
            'tr(lang, "SMALL", "МАЛ", "小锅"), 40' in display and
            "COOKER_SMALL_COOKWARE_NOTICE_MS 3000U" in config,
            "small cookware artwork shows P<36 and a localized three-second label")
    require("#define COOKER_HOT_THRESHOLD_C            60U" in config and
            "#define COOKER_HOT_IDLE_DELAY_MS         5000U" in config and
            "#define COOKER_HOT_BLINK_ON_MS           2000U" in config and
            "#define COOKER_HOT_BLINK_OFF_MS          1000U" in config and
            "picture = oled_image_hot;" in display and
            "ui_oled_show_bitmap(s_blank_frame);" in display and
            "!surface_hot" in ui and "surface_is_hot(&status)" in ui and
            "s_status.bottom_c > COOKER_HOT_THRESHOLD_C" in engine,
            "hot-surface artwork waits five seconds, blinks 2/1 and blocks automatic/manual Sleep")
    require(all(display.count(symbol) == 0 for symbol in
                ("oled_image_noopls", "oled_image_too_hot", "oled_image_what_is_going_on")),
            "reserved noopls/toohot/whatisgoingon artwork is compiled but not displayed")
    require("picture = oled_image_sleep_warning;" in display and
            "picture = oled_image_sleep;" in display and
            "picture != NULL" in display[display.find("if (show)"):
                                         display.find("vTaskDelay")] and
            "else if (sleep_clock)" in display,
            "sleep warning precedes Sleep and sleep2 overrides the clock for its first ten seconds")
    require("VIEW_START_AT_NO_CLOCK" in ui and
            'tr(lang, "TIME", "ВРЕМЯ", "时间")' in ui and
            'tr(lang, "NOT SET", "НЕ ЗАДАНО", "未设置")' in ui and
            "open_clock_editor(VIEW_START_MENU)" not in ui and
            "if (!status.clock_valid)" in ui and
            "if (cooking_schedule_absolute(target) == ESP_OK)" in ui,
            "START AT refuses an invalid clock with an explicit localized TIME NOT SET screen")
    require("#define COOKER_TEMP_MAX_C                190U" in config,
            "temperature target is capped at 190 C")
    require("powerboard_control_status_json" in web and '\\"powerboard\\":%s' in web and
            "telemetry_dropped_count" in web,
            "web status exposes read-only power/I2C diagnostics")
    require("<img" not in web and "<script src" not in web and "https://" not in web,
            "web UI is self-contained and contains no pictures or external assets")
    require("#settings label{display:block" in web and web.count("class=stage") == 5 and
            web.count("min=40 max=190") == 5 and
            all(f"id=ptime{i}" in web for i in range(1, 6)),
            "web settings are one-per-line and Profiles expose five timed 40..190 C cells")
    require("network_prod_apply_timezone" in network and "tzset()" in network,
            "timezone changes are applied without requiring a reboot")

    forbidden = ("esp_efuse_write", "esp_efuse_batch_write", "esp_flash_erase",
                 "esp_partition_erase", "esp_ota_begin", "esp_ota_write",
                 "spi_flash_write", "nvs_flash_erase")
    require(not [name for name in forbidden if name in source],
            "application has no eFuse/OTA/erase or automatic NVS erase API")
    nvs_writers = [path.name for path in MAIN.glob("*.c")
                   if "nvs_set_" in path.read_text(encoding="utf-8")]
    require(nvs_writers == ["settings.c"],
            "only the versioned settings module can persist data")
    require("nvs_set_" not in engine and "nvs_set_" not in web,
            "runtime timer/schedule/control and web handlers do not write flash directly")
    require("Never erase NVS automatically" in settings,
            "NVS initialization failure falls back to RAM without erasing stock data")
    require('nvs_set_str(handle, "wifi_ssid"' in settings and
            'nvs_set_str(handle, "wifi_pass"' in settings and
            "nvs_commit(handle)" in settings and
            "settings_wifi_get" in network,
            "Wi-Fi credentials are committed to NVS and reused after reboot")
    require('CONFIG_MCL02M_SETUP_AP_PASSWORD="12345678"' in defaults,
            "setup AP uses the documented phone-friendly password")
    require("s_settings.wifi_enabled = 0;" in settings and
            "if (settings.wifi_enabled) return network_prod_set_enabled(true);" in network and
            "settings_update(&after)" in ui and "network_prod_set_enabled" in ui and
            "network_prod_setup_password()" in ui,
            "Wi-Fi defaults OFF, persists its toggle, reconnects saved STA and shows setup password")
    require("settings_factory_reset" in settings and "nvs_erase_all(handle)" in settings and
            "case VIEW_FACTORY_CONFIRM" in ui and "esp_restart();" in ui and
            'static const char *NS = "mcl02m_v1"' in settings,
            "physical Factory reset clears only the custom namespace, including Wi-Fi/admin/profiles")

    writes = {int(value, 0) for value in
              re.findall(r"write_register\s*\(\s*(0x[0-9a-fA-F]+|\d+)", power)}
    require(writes == {0x00, 0x0C, 0x0D},
            "power-board writes remain limited to 00/0C/0D")
    require("reg >= 0x20U && reg <= 0x2fU" in
            (SHARED_POWER / "safety.h").read_text(encoding="utf-8"),
            "power-board read selector remains 0x20..0x2f")
    require("const unsigned next_period_ms = next_recovery_cycle ?" in power and
            "MCL02M_I2C_RECOVERY_HEARTBEAT_MS : MCL02M_CONTROL_HEARTBEAT_MS" in power and
            "vTaskDelayUntil(&next, pdMS_TO_TICKS(next_period_ms))" in power,
            "power pacing switches only between verified normal and I2C-recovery periods")
    ramp = power[power.find("static uint8_t next_ramped_gear"):
                 power.find("static void advance_ramp")]
    require("MCL02M_LOW_TOPOLOGY_MAX_GEAR" in ramp and
            "MCL02M_HIGH_TOPOLOGY_MIN_GEAR" in ramp and
            "candidate = MCL02M_HIGH_TOPOLOGY_MIN_GEAR" in ramp and
            "candidate = MCL02M_LOW_TOPOLOGY_MAX_GEAR" in ramp,
            "automatic ramps cross directly between low and high topologies without 36..55")
    cold_start = power[power.find("static uint8_t cold_start_first_gear"):
                       power.find("static uint8_t retained_resume_first_gear")]
    require("target <= MCL02M_LOW_START_RAMP_GEAR" in cold_start and
            "target <= MCL02M_LOW_TOPOLOGY_MAX_GEAR" in cold_start and
            "return MCL02M_LOW_START_RAMP_GEAR" in cold_start and
            "target < MCL02M_HIGH_TOPOLOGY_MIN_GEAR" in cold_start and
            "return MCL02M_MID_TOPOLOGY_MIN_GEAR" in cold_start and
            "return MCL02M_HIGH_TOPOLOGY_MIN_GEAR" in cold_start and
            "cold_start_first_gear((uint8_t)gear)" in power,
            "cold Start ramps only within the requested target's relay topology")
    require("#define MCL02M_I2C_RECOVERY_TRIGGER_CYCLES 3U" in power_safety and
            "#define MCL02M_I2C_RECOVERY_GOOD_CYCLES 2U" in power_safety and
            "#define MCL02M_I2C_RECOVERY_HEARTBEAT_MS 320U" in power_safety and
            "#define MCL02M_I2C_CRITICAL_LOSS_TIMEOUT_MS 5000U" in power_safety and
            "#define MCL02M_I2C_COMMAND_LOSS_TIMEOUT_MS 3000U" in power_safety and
            "PB_CRITICAL_READ_MASK" in power and "PB_SERVICE_READ_MASK" in power and
            "recovery_read_order" in power and "service_read_bad" in power and
            "complete_good_cycle" in power and
            "update_i2c_health_locked" in power and
            "MCL02M_I2C_BAD_CYCLES_TO_FAULT" not in power and
            "MCL02M_I2C_BAD_CYCLES_TO_FAULT" not in
            (MAIN / "CMakeLists.txt").read_text(encoding="utf-8"),
            "E09 uses critical/service classification, accelerated recovery and time limits")
    require("s_force_stop || s_status.state == PB_STATE_FAULT" in power and
            "Preserve the fault latch so the control task keeps sending Stop" in power,
            "a latched power-board fault retransmits Stop on every heartbeat")
    require("esp_task_wdt_add(NULL)" in power and "esp_task_wdt_reset()" in power and
            "CONFIG_ESP_TASK_WDT_PANIC=y" in defaults,
            "the actual power-control task is watched by a 5-second panic/reset watchdog")
    require(all(code in power for code in ("E03 HIGH VOLT", "E04 LOW VOLT", "E05 BOTTOM",
                                            "E07 IGBT", "E08 SENSOR", "E10 CHANNEL", "E12 POWER")),
            "known persistent stock R20 groups retain their E-code numbers")
    require("#define MCL02M_KNOWN_R20_FAULT_SAMPLES 2U" in power_safety and
            'if (value == 0x17) return "E07 IGBT";' in power and
            "s_r20_fault_samples >= MCL02M_KNOWN_R20_FAULT_SAMPLES" in power,
            "native E07 requires two consecutive matching R20=17 samples")
    require("MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED=1" in cmake and
            "MCL02M_MAX_IGBT_C=98U" in cmake and
            "MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES=2U" in cmake and
            "MCL02M_IGBT_START_INHIBIT_C=80U" in cmake and
            "MCL02M_RAW_SENSOR_FAULT_SAMPLES=2U" in cmake and
            "s_status.igbt_c > MCL02M_MAX_IGBT_C" in power and
            "s_igbt_limit_samples >=" in power and
            'fault_locked("IGBT INTERFACE LIMIT")' in power and
            'return "IGBT START HOT"' in power,
            "production blocks Start above 80 C and marks two samples above 98 C as interface E07")
    require("#define COOKER_IGBT_WARNING_C            92U" in config and
            "#define COOKER_IGBT_WARNING_CLEAR_C      92U" in config and
            "#define COOKER_IGBT_WARNING_SAMPLES       2U" in config and
            "#define COOKER_IGBT_WARNING_BEEP_MS     3000U" in config and
            "#define COOKER_IGBT_WARNING_RESHOW_MS   7000U" in config and
            "pb->igbt_c > COOKER_IGBT_WARNING_C" in engine and
            "pb->igbt_c < COOKER_IGBT_WARNING_CLEAR_C" in engine and
            "s_igbt_warning_samples >= COOKER_IGBT_WARNING_SAMPLES" in engine and
            "s_status.igbt_warning_active = true" in engine and
            "state_igbt_warning_enabled" in engine and
            "sound_play(SOUND_IGBT_WARNING)" in engine and
            "sound_cancel(SOUND_IGBT_WARNING)" in engine and
            "pattern == SOUND_IGBT_WARNING" in sound and
            "ui_oled_show_igbt_warning(COOKER_IGBT_WARNING_C)" in display and
            "display_prod_snooze_igbt_warning" in display and
            "display_prod_snooze_igbt_warning();" in ui and
            'snprintf(threshold, sizeof(threshold), ">%u°C", threshold_c);' in outputs and
            "case '>': return greater_than;" in outputs and
            "#define IGBT_WARNING_FREQUENCY_HZ 4000U" in sound and
            "#define IGBT_WARNING_ON_MS 300U" in sound and
            "#define IGBT_WARNING_GAP_MS 100U" in sound and
            sound.count("note(IGBT_WARNING_FREQUENCY_HZ, IGBT_WARNING_ON_MS") == 3,
            "active-session IGBT warning persists above/equal 92 C, beeps every 3 s and snoozes its screen for 7 s")
    require("FAULT_E07_INTERFACE_IGBT_LIMIT" in engine and
            'strstr(pb->fault, "IGBT INTERFACE LIMIT")' in engine and
            "ui_oled_show_bitmap_text_marked" in display and
            "60, 29" in display and
            "ui_oled_show_bitmap_text_marked" in outputs,
            "interface E07 has an OLED marker while native R20=17 E07 remains plain")
    require('fault_locked("E08 IGBT SENSOR")' in power and
            power.find('fault_locked("E08 IGBT SENSOR")') <
            power.find('fault_locked("IGBT INTERFACE LIMIT")') and
            "s_igbt_raw_fault_samples >= MCL02M_RAW_SENSOR_FAULT_SAMPLES" in power,
            "two invalid IGBT sensor samples stay a sensor fault, not temperature E07")
    require("MCL02M_MAX_BOTTOM_C=210U" in
            (MAIN / "CMakeLists.txt").read_text(encoding="utf-8") and
            "MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES=6U" in cmake and
            "return temperature_c <= MCL02M_MAX_BOTTOM_C" in power and
            "s_bottom_limit_samples >=" in power,
            "production interface cutoff needs six samples strictly above 210 C")
    require('fault_locked("E08 BOTTOM SENSOR")' in power and
            power.find('fault_locked("E08 BOTTOM SENSOR")') <
            power.find('fault_locked("BOTTOM LIMIT")') and
            "s_bottom_raw_fault_samples >= MCL02M_RAW_SENSOR_FAULT_SAMPLES" in power,
            "the six-sample 210 C cutoff retains separate two-sample NTC fault detection")
    require("#define COOKER_DELAYED_START_ATTEMPTS      2U" in config and
            "s_status.delayed_start_attempts < COOKER_DELAYED_START_ATTEMPTS" in engine and
            "scheduled_start_retry_after_timeout" in engine and
            "powerboard_control_clear_fault() == ESP_OK" in engine and
            "mapped_fault == FAULT_START_TIMEOUT" in engine,
            "Delayed Start retries both an immediate rejection and a confirmed Start timeout once")

    actual: dict[str, tuple[int, int]] = {}
    with (ROOT / "partitions.csv").open(newline="", encoding="utf-8") as stream:
        rows = (line for line in stream if not line.lstrip().startswith("#"))
        for row in csv.reader(rows, skipinitialspace=True):
            if row and row[0].strip():
                actual[row[0].strip()] = (int(row[3].strip(), 0), int(row[4].strip(), 0))
    require(actual.get("miio_fw1") == (0x10000, SLOT_SIZE) and
            actual.get("miio_fw2") == (0x170000, SLOT_SIZE),
            "stock OTA offsets and sizes are unchanged")

    binary = BUILD / "mcl02m_custom.bin"
    require(binary.is_file(), "custom app binary exists")
    require(binary.stat().st_size <= SLOT_SIZE,
            f"custom app fits ota_1 ({binary.stat().st_size:#x} <= {SLOT_SIZE:#x})")
    elf = BUILD / "mcl02m_custom.elf"
    require(elf.is_file() and b"I2C ERRORS" not in elf.read_bytes(),
            "production ELF excludes the temporary I2C-counter menu string")
    manifest = (ROOT / "BUILD_MANIFEST.md").read_text(encoding="utf-8")
    current_hash = sha256(binary)
    require(f"`{binary.stat().st_size}` bytes" in manifest,
            "build manifest size matches the current app binary")
    if os.environ.get("MCL02M_VERIFY_MANIFEST") == "1":
        require(current_hash in manifest,
                "release build SHA-256 matches the manifest")
    elif current_hash in manifest:
        print("PASS: local build SHA-256 matches the reference manifest")
    else:
        print("INFO: local build hash differs from the reference build (compile metadata may differ)")
    dump = PRIVATE / "source" / "mcl02m_esp32_flash_working.bin"
    if dump.is_file():
        require(dump.stat().st_size == 0x1000000,
                "local original recovery dump is exactly 16 MiB")
        require(sha256(dump) == DUMP_SHA256, "local original recovery dump SHA-256 matches")
    else:
        print("INFO: local recovery dump not present; public-clone recovery check skipped")
    generated_table = BUILD / "partition_table" / "partition-table.bin"
    stock_table = PRIVATE / "partitions" / "partition_table_0x00008000.bin"
    if stock_table.is_file():
        require(generated_table.read_bytes() == stock_table.read_bytes()[:generated_table.stat().st_size],
                "generated partition table is byte-identical to local stock table")
    else:
        print("INFO: local stock partition-table binary not present; CSV contract remains checked")

    print(f"INFO: app size={binary.stat().st_size} bytes")
    print(f"INFO: app sha256={current_hash}")
    print("SAFETY CHECK: PASS (offline only; no flashing authorization implied)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
