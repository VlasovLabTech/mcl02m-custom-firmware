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
    cmake = (MAIN / "CMakeLists.txt").read_text(encoding="utf-8")
    engine = (MAIN / "cooking_engine.c").read_text(encoding="utf-8")
    web = (MAIN / "web_server_prod.c").read_text(encoding="utf-8")
    sound = (MAIN / "sound.c").read_text(encoding="utf-8")
    melodies_path = MAIN / "melody_tables.h"
    melodies = melodies_path.read_text(encoding="utf-8")
    app_main = (MAIN / "app_main.c").read_text(encoding="utf-8")
    indicators = (MAIN / "indicators.c").read_text(encoding="utf-8")
    display = (MAIN / "display_prod.c").read_text(encoding="utf-8")
    ui = (MAIN / "ui_controller.c").read_text(encoding="utf-8")
    network = (MAIN / "network_prod.c").read_text(encoding="utf-8")
    defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    settings = (MAIN / "settings.c").read_text(encoding="utf-8")
    power = (SHARED_POWER / "powerboard_control.c").read_text(encoding="utf-8")
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
    require("#define COOKER_NO_PAN_TIMEOUT_MS         60000U" in config,
            "NoPan return window is 60 seconds")
    require("#define COOKER_NO_PAN_SOUND_PAUSE_MS      3000U" in config and
            "no_pan_sound_locked();" in engine and
            "sound_play(SOUND_NO_PAN);" in engine and
            "play_table(k_sound_no_pan" in sound and
            "wait_interruptible(COOKER_NO_PAN_SOUND_PAUSE_MS" in sound and
            "pattern == SOUND_NO_PAN || pattern == SOUND_CRITICAL" in sound,
            "NoPan melody repeats after a full three-second silent pause and bypasses sound-off")
    require("#define MCL02M_NO_PAN_SAMPLES 3U" in
            (SHARED_POWER / "safety.h").read_text(encoding="utf-8"),
            "NoPan is accepted after three consecutive 500-ms samples")
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
    require("s_status.state != COOK_STATE_COOKING" in engine and
            "update_timer_locked" in engine,
            "cooking countdown freezes on Pause and NoPan")
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
            "c3608697a8eadc646f6bb579623d1e5d011397c76e71f279ca06fdcced710e54" and
            all(name in melodies for name in ("k_sound_boot", "k_sound_complete",
                                              "k_sound_no_pan", "k_sound_critical",
                                              "k_sound_sleep", "k_sound_wake")),
            "the approved six-melody PWM table is embedded byte-for-byte")
    require("ui_buzzer_chirp_duty" in output_header and
            "ui_buzzer_chirp_duty" in outputs and
            "SELECTED_SOUND_SLEEP_DUTY_PERMILLE  180U" in melodies and
            "play_table(k_sound_sleep, SELECTED_SOUND_SLEEP_DUTY_PERMILLE" in sound and
            "SELECTED_SOUND_NORMAL_DUTY_PERMILLE 500U" in melodies,
            "table player supports 18-percent sleep duty and 50-percent normal duty")
    wake_branch = engine[engine.find("case INTENT_WAKE"):
                         engine.find("case INTENT_ACK")]
    require("s_status.state == COOK_STATE_SLEEP" in wake_branch and
            "sound_stop();" in wake_branch and "sound_play(SOUND_WAKE);" in wake_branch and
            "SOUND_WAKE" in sound and "play_table(k_sound_wake" in sound,
            "wake melody plays only on a real Sleep-to-Idle transition")
    require("if (s_no_pan_announced) sound_stop();" in engine and
            "pausing_no_pan && s_no_pan_announced" in engine,
            "NoPan melody loop is interrupted on pan return, Stop or Pause")
    require("sound_stop();" in engine[engine.find("case INTENT_ACK"):
                                      engine.find("case INTENT_TIMER_SET")],
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
    require("if (!state_session_open(s_status.state) && r26_valid && r26 != 0)" in power and
            "if (!state_can_energize(s_status.state) && r26_valid && r26 != 0)" not in power and
            "Preserve the first cause" in power,
            "active-zero and Pause retain the session without a false STOP VERIFY fault")
    require("retained_session_healthy_locked" in power and
            "s_status.registers[6] != 0" in power and
            "!retained_session_healthy_locked()" in power and
            'ESP_LOGW(TAG, "Z,REJECT,PAUSE' in power and
            'ESP_LOGI(TAG, "C,RESUME,%d"' in engine and
            'ESP_LOGI(TAG, "B,U,%lld"' in inputs,
            "Pause resumes only from a healthy retained session and logs every decision")
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
    require("#define COOKER_MANUAL_PAUSE_TIMEOUT_MS   (2U * 60U * 60U * 1000U)" in config and
            "update_manual_pause_timeout_locked" in engine and
            'normal_stop_locked("PAUSE TIMEOUT", false)' in engine and
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
    require("show_i2c_debug" in settings and
            "s_settings.show_i2c_debug = 0;" in settings and
            "if (stored.schema <= 4U) s_settings.show_i2c_debug = 0;" in settings and
            'case 9: return "I2C ERRORS";' in ui and
            "case 9: s_setting_value = settings.show_i2c_debug;" in ui and
            "case 9: settings.show_i2c_debug = s_setting_value;" in ui,
            "temporary I2C counter display is an opt-in persisted physical setting")
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
            "timer LED is on immediately while the two-field editor is open")
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
    require("VIEW_TIMER_DISABLE" in ui and "cooking_timer_toggle();" in ui and
            "if (s_timer_editing) close_timer_editor();" in ui,
            "an active timer requires center confirmation to disable; Timer backs out unchanged")
    require("status.state == COOK_STATE_DELAYED" in ui and "cooking_schedule_cancel();" in ui and
            "VIEW_START_IN_MINUTES" in ui and "VIEW_START_IN_HOURS" in ui,
            "center hold cancels delayed Start and START IN edits minutes then hours")
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
        "oled_image_error", "oled_image_no_pan", "oled_image_ready",
        "oled_image_sleep_warning", "oled_image_sleep", "oled_image_turn_on",
        "oled_image_wakeup",
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
            image_lengths == [384] * 10 and
            "OLED_ASSET_FRAME_BYTES == UI_OLED_BITMAP_BYTES" in display and
            "ui_oled_show_bitmap" in outputs and "ui_oled_show_bitmap_text" in outputs,
            "all ten approved 64x48 pictures compile as exact 384-byte OLED frames")
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
            "error artwork removes the separator/example and renders the live code at 2x")
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
    require("display_prod_show_confirm();" in ui and
            "display_prod_show_cancel();" in ui and
            "TRANSIENT_CONFIRM" in display and "TRANSIENT_CANCEL" in display,
            "settings confirmation and physical Cancel select their timed pictures")
    require("picture = oled_image_ready;" in display and
            "picture = oled_image_error;" in display and
            "picture = oled_image_no_pan;" in display and
            "status.state == COOK_STATE_COMPLETE) &&" not in ui,
            "ready, error and no-pan pictures remain state-latched until user or pan action")
    require("picture = oled_image_sleep_warning;" in display and
            "picture = oled_image_sleep;" in display and
            "picture != NULL" in display[display.find("if (show)"):
                                         display.find("vTaskDelay")] and
            "else if (sleep_clock)" in display,
            "sleep warning precedes Sleep and sleep2 overrides the clock for its first ten seconds")
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
    require("vTaskDelayUntil(&next, pdMS_TO_TICKS(MCL02M_CONTROL_HEARTBEAT_MS))" in power,
            "verified 500-ms power heartbeat remains the sole pacing loop")
    require("MCL02M_I2C_BAD_CYCLES_TO_FAULT=6U" in
            (MAIN / "CMakeLists.txt").read_text(encoding="utf-8") and
            "#define MCL02M_I2C_BAD_CYCLES_TO_FAULT 2U" in power_safety,
            "E09 requires six consecutive bad 500-ms I2C cycles")
    require("s_force_stop || s_status.state == PB_STATE_FAULT" in power and
            "Preserve the fault latch so the control task keeps sending Stop" in power,
            "a latched power-board fault retransmits Stop on every heartbeat")
    require("esp_task_wdt_add(NULL)" in power and "esp_task_wdt_reset()" in power and
            "CONFIG_ESP_TASK_WDT_PANIC=y" in defaults,
            "the actual power-control task is watched by a 5-second panic/reset watchdog")
    require(all(code in power for code in ("E03 HIGH VOLT", "E04 LOW VOLT", "E05 BOTTOM",
                                            "E07 IGBT", "E08 SENSOR", "E10 CHANNEL", "E12 POWER")),
            "known persistent stock R20 groups retain their E-code numbers")
    require("MCL02M_MAX_BOTTOM_C=210U" in
            (MAIN / "CMakeLists.txt").read_text(encoding="utf-8") and
            "bottom_temperature_interface_safe" in power,
            "production interface cutoff is 210 C above the 190 C setpoint range")
    require('fault_locked("E08 BOTTOM SENSOR")' in power and
            power.find('fault_locked("E08 BOTTOM SENSOR")') <
            power.find('fault_locked("BOTTOM LIMIT")'),
            "the 210 C production cutoff retains separate NTC fault detection")

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
