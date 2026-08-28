#include "cooking_engine.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "powerboard_control.h"
#include "settings.h"
#include "sound.h"
#include "telemetry.h"
#include "temperature_ctrl.h"

#ifndef MCL02M_ACTIVE_ZERO_DIAGNOSTICS
#define MCL02M_ACTIVE_ZERO_DIAGNOSTICS 0
#endif

#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
static const char *TAG = "cookdbg";
#endif

typedef enum {
    INTENT_SET_MODE,
    INTENT_SET_POWER,
    INTENT_SET_TEMP,
    INTENT_STOP,
    INTENT_PAUSE_RESUME,
    INTENT_SLEEP,
    INTENT_WAKE,
    INTENT_ACK,
    INTENT_TIMER_SET,
    INTENT_TIMER_TOGGLE,
    INTENT_SCHEDULE_REL,
    INTENT_SCHEDULE_ABS,
    INTENT_SCHEDULE_CANCEL,
} intent_type_t;

typedef struct {
    intent_type_t type;
    int64_t value;
    bool flag;
    char reason[24];
} intent_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static cooker_snapshot_t s_status;
static temperature_ctrl_t s_temperature;
static int64_t s_last_tick_us;
static int64_t s_last_temp_update_us;
static int64_t s_timer_accumulator_us;
static int64_t s_no_pan_since_us;
static int64_t s_run_started_us;
static int64_t s_delayed_due_mono_us;
static int64_t s_manual_pause_since_us;
static bool s_saturation_announced;
static bool s_no_pan_announced;
static bool s_waiting_pan_before_start;
static bool s_active_zero;
static cooker_profile_t s_active_profile;
static bool s_profile_selected;

static cook_mode_t control_mode_locked(void)
{
    return s_status.mode == COOK_MODE_PROFILE ?
           (cook_mode_t)s_status.profile_stage_mode : s_status.mode;
}

static void no_pan_sound_locked(void)
{
    if (!s_no_pan_announced) {
        s_no_pan_announced = true;
        sound_play(SOUND_NO_PAN);
    }
}

const char *cooking_state_name(cook_state_t state)
{
    static const char *names[] = {
        "SLEEP", "IDLE", "READY", "DELAYED", "STARTING", "COOKING",
        "PAUSED", "NO_PAN", "COMPLETE", "FAULT"
    };
    return state <= COOK_STATE_FAULT ? names[state] : "UNKNOWN";
}

const char *cooking_fault_name(cooker_fault_t fault)
{
    switch (fault) {
    case FAULT_NONE: return "NONE";
    case FAULT_E02_NO_PAN_TIMEOUT: return "E02 NO PAN";
    case FAULT_E03_HIGH_VOLTAGE: return "E03 HIGH VOLT";
    case FAULT_E04_LOW_VOLTAGE: return "E04 LOW VOLT";
    case FAULT_E05_BOTTOM_OVERHEAT: return "E05 BOTTOM HOT";
    case FAULT_E07_IGBT_OVERHEAT: return "E07 IGBT HOT";
    case FAULT_E08_SENSOR: return "E08 SENSOR";
    case FAULT_E09_COMMUNICATION: return "E09 I2C LOST";
    case FAULT_E10_WIRE_OR_CHANNEL: return "E10 CHANNEL";
    case FAULT_E12_POWER_STATUS: return "E12 POWER";
    case FAULT_POWER_STATUS: return "POWER STATUS";
    case FAULT_START_TIMEOUT: return "START TIMEOUT";
    case FAULT_HARD_RUN_LIMIT: return "RUN LIMIT";
    default: return "UNKNOWN";
    }
}

static bool state_active(cook_state_t state)
{
    return state == COOK_STATE_STARTING || state == COOK_STATE_COOKING ||
           state == COOK_STATE_PAUSED || state == COOK_STATE_NO_PAN;
}

static bool state_schedulable(cook_state_t state)
{
    return state == COOK_STATE_SLEEP || state == COOK_STATE_IDLE ||
           state == COOK_STATE_READY || state == COOK_STATE_COMPLETE;
}

static void emit_status(const char *event)
{
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"cooking\",\"event\":\"%s\","
                    "\"state\":\"%s\",\"mode\":%u,\"gear\":%u,\"temp\":%u,"
                    "\"active_zero\":%s,\"pause_s\":%" PRIu32 "}",
                    esp_timer_get_time() / 1000, event, cooking_state_name(s_status.state),
                    s_status.mode, s_status.applied_gear, s_status.bottom_c,
                    s_status.active_zero ? "true" : "false", s_status.pause_remaining_s);
}

static void set_fault_locked(cooker_fault_t fault, const char *detail)
{
    if (s_status.state == COOK_STATE_FAULT && s_status.fault == fault) return;
    powerboard_control_stop(detail == NULL ? "FAULT" : detail);
    s_status.state = COOK_STATE_FAULT;
    s_status.fault = fault;
    s_status.delayed_start = false;
    s_status.timer_enabled = false;
    s_waiting_pan_before_start = false;
    s_active_zero = false;
    s_status.active_zero = false;
    s_status.pause_remaining_s = 0;
    s_manual_pause_since_us = 0;
    strlcpy(s_status.detail, detail == NULL ? cooking_fault_name(fault) : detail,
            sizeof(s_status.detail));
    sound_stop();
    sound_play(SOUND_CRITICAL);
    emit_status("fault");
}

static cooker_fault_t map_power_fault(const powerboard_status_t *pb)
{
    if (strstr(pb->fault, "E03") != NULL) return FAULT_E03_HIGH_VOLTAGE;
    if (strstr(pb->fault, "E04") != NULL) return FAULT_E04_LOW_VOLTAGE;
    if (strstr(pb->fault, "E05") != NULL) return FAULT_E05_BOTTOM_OVERHEAT;
    if (strstr(pb->fault, "E07") != NULL) return FAULT_E07_IGBT_OVERHEAT;
    if (strstr(pb->fault, "E08") != NULL) return FAULT_E08_SENSOR;
    if (strstr(pb->fault, "E10") != NULL) return FAULT_E10_WIRE_OR_CHANNEL;
    if (strstr(pb->fault, "E12") != NULL) return FAULT_E12_POWER_STATUS;
    if (strstr(pb->fault, "IGBT") != NULL) return FAULT_E07_IGBT_OVERHEAT;
    if (strstr(pb->fault, "BOTTOM") != NULL) return FAULT_E05_BOTTOM_OVERHEAT;
    if (strstr(pb->fault, "I2C") != NULL) return FAULT_E09_COMMUNICATION;
    if (strstr(pb->fault, "START") != NULL) return FAULT_START_TIMEOUT;
    return FAULT_POWER_STATUS;
}

static uint8_t initial_temperature_gear_locked(void)
{
    temperature_ctrl_reset(&s_temperature);
    temperature_ctrl_observe(&s_temperature, s_status.bottom_c);
    const uint8_t gear = temperature_ctrl_update(&s_temperature,
                                                  s_status.target_temperature_c,
                                                  s_status.bottom_c,
                                                  COOKER_TEMP_UPDATE_MS);
    s_last_temp_update_us = esp_timer_get_time();
    return gear;
}

static bool prepare_profile_stage_locked(unsigned start, bool running)
{
    if (!s_profile_selected) return false;
    for (unsigned i = start; i < COOKER_PROFILE_STAGE_COUNT; ++i) {
        const cooker_profile_stage_t *stage = &s_active_profile.stages[i];
        if (stage->timer_s == 0) continue;
        s_status.profile_stage_index = (uint8_t)(i + 1U);
        s_status.profile_stage_count = COOKER_PROFILE_STAGE_COUNT;
        s_status.profile_stage_mode = stage->mode;
        s_status.selected_gear = stage->gear;
        s_status.target_temperature_c = stage->temperature_c;
        s_status.timer_last_s = stage->timer_s;
        s_status.timer_remaining_s = stage->timer_s;
        s_status.timer_enabled = running;
        s_status.temp_phase = TEMP_PHASE_OFF;
        s_status.hold_saturated = false;
        s_saturation_announced = false;
        s_last_temp_update_us = 0;
        s_timer_accumulator_us = 0;
        return true;
    }
    return false;
}

static esp_err_t select_profile_locked(unsigned index)
{
    if (index >= COOKER_PROFILE_COUNT || state_active(s_status.state) ||
        s_status.state == COOK_STATE_FAULT)
        return ESP_ERR_INVALID_STATE;
    cooker_profile_t profiles[COOKER_PROFILE_COUNT];
    settings_profiles_get(profiles);
    if (settings_profile_stage_count(&profiles[index]) == 0)
        return ESP_ERR_INVALID_ARG;
    s_active_profile = profiles[index];
    s_profile_selected = true;
    s_status.mode = COOK_MODE_PROFILE;
    s_status.profile_index = (uint8_t)(index + 1U);
    s_status.state = COOK_STATE_READY;
    if (!prepare_profile_stage_locked(0, false)) return ESP_ERR_INVALID_ARG;
    strlcpy(s_status.detail, "PROFILE READY", sizeof(s_status.detail));
    emit_status("profile_selected");
    return ESP_OK;
}

static esp_err_t begin_run_locked(void)
{
    if (s_status.state == COOK_STATE_FAULT || state_active(s_status.state))
        return ESP_ERR_INVALID_STATE;

    if (s_status.mode == COOK_MODE_PROFILE && !prepare_profile_stage_locked(0, true))
        return ESP_ERR_INVALID_ARG;
    uint8_t gear = s_status.selected_gear;
    const bool temperature_mode = control_mode_locked() == COOK_MODE_TEMPERATURE;
    if (temperature_mode)
        gear = initial_temperature_gear_locked();
    if (gear > COOKER_MAX_GEAR) return ESP_ERR_INVALID_ARG;

    esp_err_t err = powerboard_control_arm(COOKER_POWERBOARD_ARM_MS);
    if (err == ESP_OK)
        err = powerboard_control_start(gear, COOKER_HARD_RUN_LIMIT_MS);
    if (err != ESP_OK) {
        if (s_status.mode == COOK_MODE_PROFILE) s_status.timer_enabled = false;
        return err;
    }

    s_active_zero = gear == 0;
    s_status.active_zero = s_active_zero;
    s_status.state = s_active_zero ? COOK_STATE_COOKING : COOK_STATE_STARTING;
    s_status.fault = FAULT_NONE;
    s_status.applied_gear = gear;
    s_status.paused_gear = 0;
    s_status.delayed_start = false;
    s_status.hold_saturated = false;
    s_saturation_announced = false;
    s_no_pan_announced = false;
    s_waiting_pan_before_start = false;
    s_no_pan_since_us = 0;
    s_run_started_us = esp_timer_get_time();
    s_timer_accumulator_us = 0;
    strlcpy(s_status.detail, s_active_zero ? "ACTIVE ZERO" : "STARTING",
            sizeof(s_status.detail));
    emit_status("start");
    return ESP_OK;
}

static void normal_stop_locked(const char *reason, bool complete)
{
    if (s_no_pan_announced) sound_stop();
    powerboard_control_stop(reason);
    s_status.state = complete ? COOK_STATE_COMPLETE : COOK_STATE_IDLE;
    s_status.applied_gear = 0;
    s_status.paused_gear = 0;
    s_status.temp_phase = TEMP_PHASE_OFF;
    s_status.delayed_start = false;
    s_status.hold_saturated = false;
    s_status.timer_enabled = false;
    s_waiting_pan_before_start = false;
    s_active_zero = false;
    s_status.active_zero = false;
    s_status.pause_remaining_s = 0;
    s_manual_pause_since_us = 0;
    if (!complete) s_status.timer_remaining_s = s_status.timer_last_s;
    s_no_pan_since_us = 0;
    s_no_pan_announced = false;
    if (complete) sound_play(SOUND_COMPLETE);
    strlcpy(s_status.detail, reason == NULL ? "STOP" : reason, sizeof(s_status.detail));
    emit_status(complete ? "complete" : "stop");
}

static esp_err_t apply_output_locked(uint8_t gear)
{
    if (gear > COOKER_MAX_GEAR) return ESP_ERR_INVALID_ARG;
    powerboard_status_t pb;
    powerboard_control_get_status(&pb);
    if (pb.state == PB_STATE_STOPPED || pb.state == PB_STATE_ARMED ||
        pb.state == PB_STATE_FAULT || pb.state == PB_STATE_BOOT)
        return ESP_ERR_INVALID_STATE;

    const bool was_active_zero = s_active_zero || pb.state == PB_STATE_ACTIVE_ZERO;
    const esp_err_t err = powerboard_control_set_gear(gear);
    if (err == ESP_OK) {
        s_active_zero = gear == 0;
        s_status.active_zero = s_active_zero || s_status.state == COOK_STATE_PAUSED;
        s_status.applied_gear = gear;
        if (s_status.state != COOK_STATE_PAUSED) {
            if (gear == 0) {
                if (s_no_pan_announced) sound_stop();
                s_no_pan_announced = false;
                s_no_pan_since_us = 0;
                s_status.state = COOK_STATE_COOKING;
                strlcpy(s_status.detail, "ACTIVE ZERO", sizeof(s_status.detail));
                emit_status("active_zero");
            } else if (was_active_zero) {
                s_status.state = COOK_STATE_STARTING;
                strlcpy(s_status.detail, "RESUMING HEAT", sizeof(s_status.detail));
                emit_status("active_zero_resume");
            }
        }
    }
    return err;
}

static void update_manual_pause_timeout_locked(int64_t now_us)
{
    if (s_status.state != COOK_STATE_PAUSED || s_manual_pause_since_us == 0) {
        s_status.pause_remaining_s = 0;
        return;
    }
    const int64_t timeout_us = (int64_t)COOKER_MANUAL_PAUSE_TIMEOUT_MS * 1000LL;
    const int64_t elapsed_us = now_us - s_manual_pause_since_us;
    if (elapsed_us >= timeout_us) {
        normal_stop_locked("PAUSE TIMEOUT", false);
        return;
    }
    s_status.pause_remaining_s = (uint32_t)((timeout_us - elapsed_us + 999999LL) / 1000000LL);
}

static void update_timer_locked(int64_t delta_us)
{
    if (!s_status.timer_enabled || s_status.timer_remaining_s == 0 ||
        s_status.state != COOK_STATE_COOKING) return;
    s_timer_accumulator_us += delta_us;
    while (s_timer_accumulator_us >= 1000000 && s_status.timer_remaining_s > 0) {
        s_timer_accumulator_us -= 1000000;
        --s_status.timer_remaining_s;
    }
    if (s_status.timer_remaining_s == 0) {
        if (s_status.mode == COOK_MODE_PROFILE) {
            const unsigned next = s_status.profile_stage_index;
            if (prepare_profile_stage_locked(next, true)) {
                uint8_t gear = s_status.selected_gear;
                if (control_mode_locked() == COOK_MODE_TEMPERATURE)
                    gear = initial_temperature_gear_locked();
                if (apply_output_locked(gear) != ESP_OK) {
                    set_fault_locked(FAULT_POWER_STATUS, "PROFILE STAGE FAILED");
                    return;
                }
                s_status.applied_gear = gear;
                sound_play(SOUND_STAGE);
                strlcpy(s_status.detail, "PROFILE NEXT", sizeof(s_status.detail));
                emit_status("profile_stage");
            } else {
                s_status.timer_enabled = false;
                normal_stop_locked("PROFILE COMPLETE", true);
            }
        } else {
            s_status.timer_enabled = false;
            normal_stop_locked("TIMER COMPLETE", true);
        }
    }
}

static void update_temperature_locked(int64_t now_us)
{
    if (control_mode_locked() != COOK_MODE_TEMPERATURE ||
        !state_active(s_status.state) ||
        !s_status.readings_valid || now_us - s_last_temp_update_us < COOKER_TEMP_UPDATE_MS * 1000LL)
        return;
    const uint32_t elapsed = s_last_temp_update_us == 0 ? COOKER_TEMP_UPDATE_MS :
                             (uint32_t)((now_us - s_last_temp_update_us) / 1000);
    s_last_temp_update_us = now_us;
    temperature_ctrl_observe(&s_temperature, s_status.bottom_c);
    if (s_status.state != COOK_STATE_COOKING) return;
    const uint8_t gear = temperature_ctrl_update(&s_temperature,
                                                  s_status.target_temperature_c,
                                                  s_status.bottom_c, elapsed);
    s_status.temp_phase = s_temperature.phase;
    if (gear != s_status.applied_gear && apply_output_locked(gear) == ESP_OK)
        s_status.applied_gear = gear;
    s_status.hold_saturated = s_temperature.saturated;
    if (s_status.hold_saturated && !s_saturation_announced) {
        s_saturation_announced = true;
        strlcpy(s_status.detail, "HOLD SATURATED", sizeof(s_status.detail));
        sound_play(SOUND_WARNING);
        emit_status("hold_saturated");
    }
}

static void apply_power_status_locked(const powerboard_status_t *pb, int64_t now_us)
{
    s_status.applied_gear = pb->applied_gear;
    s_status.igbt_c = pb->igbt_c;
    s_status.bottom_c = pb->bottom_c;
    s_status.i2c_bad_cycles = (uint8_t)(pb->consecutive_bad_cycles > COOKER_I2C_DEBUG_MAX ?
                                        COOKER_I2C_DEBUG_MAX :
                                        pb->consecutive_bad_cycles);
    s_status.readings_valid = (pb->valid_mask & ((1U << 3) | (1U << 4))) ==
                              ((1U << 3) | (1U << 4));
    if (pb->valid_mask & (1U << 2)) s_status.mains_voltage_v = pb->registers[2] + 50U;
    s_status.pan_present = pb->state != PB_STATE_NO_PAN;
    s_status.active_zero = pb->state == PB_STATE_ACTIVE_ZERO || pb->state == PB_STATE_PAUSED;
    s_status.run_elapsed_s = s_run_started_us == 0 ? 0 :
                             (uint32_t)((now_us - s_run_started_us) / 1000000);

    if (pb->state == PB_STATE_FAULT) {
        set_fault_locked(map_power_fault(pb), pb->fault);
        return;
    }
    if (s_waiting_pan_before_start) {
        const bool r20_valid = (pb->valid_mask & 1U) != 0;
        s_status.pan_present = r20_valid && pb->registers[0] == 0;
        if (s_status.pan_present) {
            if (s_no_pan_announced) sound_stop();
            s_waiting_pan_before_start = false;
            s_no_pan_since_us = 0;
            s_no_pan_announced = false;
            s_status.state = COOK_STATE_READY;
            if (begin_run_locked() != ESP_OK)
                set_fault_locked(FAULT_START_TIMEOUT, "SCHEDULE START BLOCKED");
        } else {
            no_pan_sound_locked();
            if (now_us - s_no_pan_since_us >= COOKER_NO_PAN_TIMEOUT_MS * 1000LL)
                set_fault_locked(FAULT_E02_NO_PAN_TIMEOUT, "E02 NO PAN TIMEOUT");
        }
        return;
    }
    if (!state_active(s_status.state)) return;

    if (pb->state == PB_STATE_NO_PAN) {
        if (s_no_pan_since_us == 0) {
            s_no_pan_since_us = now_us;
        }
        no_pan_sound_locked();
        s_status.state = COOK_STATE_NO_PAN;
        strlcpy(s_status.detail, "NO PAN", sizeof(s_status.detail));
        if (now_us - s_no_pan_since_us >= COOKER_NO_PAN_TIMEOUT_MS * 1000LL)
            set_fault_locked(FAULT_E02_NO_PAN_TIMEOUT, "E02 NO PAN TIMEOUT");
        return;
    }
    if (s_status.state == COOK_STATE_NO_PAN &&
        (pb->state == PB_STATE_STARTING || pb->state == PB_STATE_HEATING)) {
        if (s_no_pan_announced) sound_stop();
        s_no_pan_since_us = 0;
        s_no_pan_announced = false;
        s_status.state = pb->state == PB_STATE_HEATING ? COOK_STATE_COOKING : COOK_STATE_STARTING;
        strlcpy(s_status.detail, "PAN RETURNED", sizeof(s_status.detail));
    }
    if (pb->state == PB_STATE_HEATING && s_status.state == COOK_STATE_STARTING) {
        s_status.state = COOK_STATE_COOKING;
        strlcpy(s_status.detail, "COOKING", sizeof(s_status.detail));
        emit_status("heating_confirmed");
    }
    if (pb->state == PB_STATE_STOPPED && state_active(s_status.state))
        set_fault_locked(FAULT_HARD_RUN_LIMIT, "UNEXPECTED STOP");
}

static void handle_intent_locked(const intent_t *intent)
{
    const int64_t now = esp_timer_get_time();
    switch (intent->type) {
    case INTENT_SET_MODE:
        if (!state_active(s_status.state) && s_status.state != COOK_STATE_FAULT) {
            s_status.mode = (cook_mode_t)intent->value;
            if (s_status.mode != COOK_MODE_PROFILE) s_profile_selected = false;
            s_status.state = COOK_STATE_READY;
        }
        break;
    case INTENT_SET_POWER:
        if (intent->value >= 0 && intent->value <= COOKER_MAX_GEAR) {
            s_status.selected_gear = (uint8_t)intent->value;
            if (s_status.mode == COOK_MODE_POWER &&
                (s_status.state == COOK_STATE_COOKING || s_status.state == COOK_STATE_STARTING ||
                 s_status.state == COOK_STATE_NO_PAN || s_status.state == COOK_STATE_PAUSED) &&
                apply_output_locked((uint8_t)intent->value) == ESP_OK) {
                if (s_status.state == COOK_STATE_PAUSED)
                    s_status.paused_gear = (uint8_t)intent->value;
            }
        }
        break;
    case INTENT_SET_TEMP:
        if (intent->value >= COOKER_TEMP_MIN_C && intent->value <= COOKER_TEMP_MAX_C)
            s_status.target_temperature_c = (uint16_t)intent->value;
        break;
    case INTENT_STOP:
        if (state_active(s_status.state) || s_status.state == COOK_STATE_DELAYED)
            normal_stop_locked(intent->reason, false);
        else if (s_status.state == COOK_STATE_COMPLETE) s_status.state = COOK_STATE_IDLE;
        break;
    case INTENT_PAUSE_RESUME:
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
        ESP_LOGI(TAG, "C,PR,%s", cooking_state_name(s_status.state));
#endif
        if (s_status.state == COOK_STATE_COOKING || s_status.state == COOK_STATE_STARTING ||
            s_status.state == COOK_STATE_NO_PAN) {
            const bool pausing_no_pan = s_status.state == COOK_STATE_NO_PAN;
            s_status.paused_gear = s_status.applied_gear;
            const esp_err_t pause_err = powerboard_control_pause();
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
            ESP_LOGI(TAG, "C,PAUSE,%d", pause_err);
#endif
            if (pause_err == ESP_OK) {
                if (pausing_no_pan && s_no_pan_announced) sound_stop();
                if (pausing_no_pan) s_no_pan_announced = false;
                if (control_mode_locked() == COOK_MODE_TEMPERATURE) {
                    temperature_ctrl_restart(&s_temperature);
                    s_status.hold_saturated = false;
                    s_saturation_announced = false;
                }
                s_status.state = COOK_STATE_PAUSED;
                s_status.active_zero = true;
                s_manual_pause_since_us = now;
                s_status.pause_remaining_s = COOKER_MANUAL_PAUSE_TIMEOUT_MS / 1000U;
                strlcpy(s_status.detail, "PAUSED", sizeof(s_status.detail));
                emit_status("manual_pause");
            }
        } else if (s_status.state == COOK_STATE_PAUSED) {
            esp_err_t resume_err = ESP_OK;
            if (control_mode_locked() == COOK_MODE_TEMPERATURE) {
                if (!s_status.readings_valid) {
                    resume_err = ESP_ERR_INVALID_STATE;
                } else {
                    temperature_ctrl_restart(&s_temperature);
                    const uint8_t resume_gear = temperature_ctrl_update(
                        &s_temperature, s_status.target_temperature_c,
                        s_status.bottom_c, COOKER_TEMP_UPDATE_MS);
                    s_status.temp_phase = s_temperature.phase;
                    s_status.hold_saturated = s_temperature.saturated;
                    resume_err = apply_output_locked(resume_gear);
                }
            }
            if (resume_err == ESP_OK) resume_err = powerboard_control_resume();
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
            ESP_LOGI(TAG, "C,RESUME,%d", resume_err);
#endif
            if (resume_err == ESP_OK) {
                s_last_temp_update_us = now;
                s_manual_pause_since_us = 0;
                s_status.pause_remaining_s = 0;
                s_status.active_zero = s_active_zero;
                s_status.state = s_active_zero ? COOK_STATE_COOKING : COOK_STATE_STARTING;
                strlcpy(s_status.detail, s_active_zero ? "ACTIVE ZERO" : "RESUMING",
                        sizeof(s_status.detail));
                emit_status("manual_resume");
            }
        }
        break;
    case INTENT_SLEEP:
        if (!state_active(s_status.state) && s_status.state != COOK_STATE_FAULT &&
            s_status.state != COOK_STATE_DELAYED) s_status.state = COOK_STATE_SLEEP;
        break;
    case INTENT_WAKE:
        if (s_status.state == COOK_STATE_SLEEP) {
            s_status.state = COOK_STATE_IDLE;
            sound_stop();
            sound_play(SOUND_WAKE);
        } else if (s_status.state == COOK_STATE_COMPLETE) {
            s_status.state = COOK_STATE_IDLE;
        }
        break;
    case INTENT_ACK:
        if (s_status.state == COOK_STATE_FAULT) {
            powerboard_status_t pb;
            powerboard_control_get_status(&pb);
            const esp_err_t clear = pb.state == PB_STATE_FAULT ?
                                    powerboard_control_clear_fault() :
                                    (pb.state == PB_STATE_STOPPED ? ESP_OK : ESP_ERR_INVALID_STATE);
            if (clear == ESP_OK) {
                sound_stop();
                s_status.fault = FAULT_NONE;
                s_status.state = COOK_STATE_IDLE;
                s_status.applied_gear = 0;
                strlcpy(s_status.detail, "ACKNOWLEDGED", sizeof(s_status.detail));
            }
        } else if (s_status.state == COOK_STATE_COMPLETE) s_status.state = COOK_STATE_IDLE;
        break;
    case INTENT_TIMER_SET:
        if (s_status.mode != COOK_MODE_PROFILE &&
            intent->value >= 0 && intent->value <= COOKER_MAX_TIMER_S) {
            s_status.timer_last_s = (uint32_t)intent->value;
            s_status.timer_remaining_s = (uint32_t)intent->value;
            s_status.timer_enabled = intent->flag && intent->value > 0;
            s_timer_accumulator_us = 0;
        }
        break;
    case INTENT_TIMER_TOGGLE:
        if (s_status.mode != COOK_MODE_PROFILE) {
            s_status.timer_enabled = !s_status.timer_enabled && s_status.timer_last_s > 0;
            if (s_status.timer_enabled) s_status.timer_remaining_s = s_status.timer_last_s;
        }
        break;
    case INTENT_SCHEDULE_REL:
        if (state_schedulable(s_status.state) &&
            intent->value >= 1 && intent->value <= 24 * 60 * 60) {
            s_delayed_due_mono_us = now + intent->value * 1000000LL;
            s_status.delayed_start = true;
            s_status.delayed_absolute = false;
            s_status.delayed_remaining_s = (uint32_t)intent->value;
            s_status.state = COOK_STATE_DELAYED;
        }
        break;
    case INTENT_SCHEDULE_ABS:
        if (state_schedulable(s_status.state) &&
            s_status.clock_valid && intent->value > time(NULL)) {
            s_status.delayed_epoch_s = intent->value;
            s_status.delayed_absolute = true;
            s_status.delayed_start = true;
            s_status.state = COOK_STATE_DELAYED;
        }
        break;
    case INTENT_SCHEDULE_CANCEL:
        if (s_waiting_pan_before_start && s_no_pan_announced) sound_stop();
        if (s_status.state == COOK_STATE_DELAYED || s_waiting_pan_before_start)
            s_status.state = COOK_STATE_IDLE;
        s_status.delayed_start = false;
        s_waiting_pan_before_start = false;
        s_delayed_due_mono_us = 0;
        break;
    }
}

static void update_schedule_locked(int64_t now_us)
{
    const time_t wall = time(NULL);
    s_status.clock_valid = wall > 1700000000;
    if (!s_status.delayed_start || s_status.state != COOK_STATE_DELAYED) return;
    bool due = false;
    if (s_status.delayed_absolute) {
        const int64_t remaining = s_status.delayed_epoch_s - wall;
        s_status.delayed_remaining_s = remaining > 0 ? (uint32_t)remaining : 0;
        due = s_status.clock_valid && remaining <= 0;
    } else {
        const int64_t remaining_us = s_delayed_due_mono_us - now_us;
        s_status.delayed_remaining_s = remaining_us > 0 ?
                                      (uint32_t)((remaining_us + 999999) / 1000000) : 0;
        due = remaining_us <= 0;
    }
    if (due) {
        const esp_err_t err = begin_run_locked(); /* Already physically confirmed. */
        if (err != ESP_OK) {
            powerboard_status_t pb;
            powerboard_control_get_status(&pb);
            const bool no_pan = (pb.valid_mask & 1U) != 0 && pb.registers[0] == 0x02;
            if (no_pan) {
                s_waiting_pan_before_start = true;
                s_status.delayed_start = false;
                s_status.state = COOK_STATE_NO_PAN;
                s_status.pan_present = false;
                s_no_pan_since_us = now_us;
                s_no_pan_announced = false;
                strlcpy(s_status.detail, "WAIT PAN", sizeof(s_status.detail));
                no_pan_sound_locked();
            } else {
                set_fault_locked(FAULT_START_TIMEOUT, "SCHEDULE START BLOCKED");
            }
        }
    }
}

static void engine_task(void *arg)
{
    (void)arg;
    s_last_tick_us = esp_timer_get_time();
    for (;;) {
        intent_t intent;
        while (xQueueReceive(s_queue, &intent, 0) == pdTRUE) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            handle_intent_locked(&intent);
            xSemaphoreGive(s_lock);
        }
        const int64_t now = esp_timer_get_time();
        powerboard_status_t pb;
        powerboard_control_get_status(&pb);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const int64_t delta = now - s_last_tick_us;
        s_last_tick_us = now;
        update_schedule_locked(now);
        apply_power_status_locked(&pb, now);
        update_manual_pause_timeout_locked(now);
        update_timer_locked(delta);
        update_temperature_locked(now);
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(COOKER_CONTROL_PERIOD_MS));
    }
}

esp_err_t cooking_engine_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(24, sizeof(intent_t));
    if (s_lock == NULL || s_queue == NULL) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = COOK_STATE_IDLE;
    s_status.mode = COOK_MODE_POWER;
    s_status.selected_gear = 1;
    s_status.target_temperature_c = 100;
    s_status.timer_last_s = 270;
    s_status.pan_present = true;
    s_status.profile_stage_mode = COOK_MODE_POWER;
    strlcpy(s_status.detail, "READY", sizeof(s_status.detail));
    const esp_err_t err = powerboard_control_init();
    if (err != ESP_OK) return err;
    return xTaskCreate(engine_task, "cooking", 7168, NULL, 8, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t post(intent_type_t type, int64_t value, bool flag, const char *reason)
{
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    intent_t intent = {.type = type, .value = value, .flag = flag};
    if (reason != NULL) strlcpy(intent.reason, reason, sizeof(intent.reason));
    return xQueueSend(s_queue, &intent, pdMS_TO_TICKS(100)) == pdTRUE ?
           ESP_OK : ESP_ERR_TIMEOUT;
}

void cooking_engine_get_snapshot(cooker_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *snapshot = s_status;
    xSemaphoreGive(s_lock);
}

size_t cooking_engine_status_json(char *output, size_t output_size)
{
    cooker_snapshot_t s;
    cooking_engine_get_snapshot(&s);
    return snprintf(output, output_size,
        "{\"state\":\"%s\",\"mode\":%u,\"phase\":%u,\"fault\":\"%s\","
        "\"selected_gear\":%u,\"applied_gear\":%u,\"paused_gear\":%u,\"target_c\":%u,"
        "\"bottom_c\":%u,\"igbt_c\":%u,\"i2c_bad_cycles\":%u,"
        "\"voltage_v\":%u,\"readings_valid\":%s,"
        "\"pan\":%s,\"active_zero\":%s,\"pause_remaining_s\":%" PRIu32 ","
        "\"timer_enabled\":%s,\"timer_s\":%" PRIu32 ","
        "\"timer_last_s\":%" PRIu32 ",\"delayed\":%s,\"delayed_s\":%" PRIu32 ","
        "\"clock_valid\":%s,\"hold_saturated\":%s,"
        "\"profile\":%u,\"profile_stage\":%u,\"profile_cells\":%u,"
        "\"profile_mode\":%u,"
        "\"run_s\":%" PRIu32 ",\"detail\":\"%s\"}",
        cooking_state_name(s.state), s.mode, s.temp_phase, cooking_fault_name(s.fault),
        s.selected_gear, s.applied_gear, s.paused_gear, s.target_temperature_c,
        s.bottom_c, s.igbt_c, s.i2c_bad_cycles,
        s.mains_voltage_v, s.readings_valid ? "true" : "false", s.pan_present ? "true" : "false",
        s.active_zero ? "true" : "false", s.pause_remaining_s,
        s.timer_enabled ? "true" : "false", s.timer_remaining_s, s.timer_last_s,
        s.delayed_start ? "true" : "false", s.delayed_remaining_s,
        s.clock_valid ? "true" : "false", s.hold_saturated ? "true" : "false",
        s.profile_index, s.profile_stage_index, s.profile_stage_count,
        s.profile_stage_mode,
        s.run_elapsed_s, s.detail);
}

esp_err_t cooking_set_mode(cook_mode_t mode)
{
    return mode <= COOK_MODE_TEMPERATURE ?
           post(INTENT_SET_MODE, mode, false, NULL) : ESP_ERR_INVALID_ARG;
}
esp_err_t cooking_set_power(unsigned gear) { return gear <= 99 ? post(INTENT_SET_POWER, gear, false, NULL) : ESP_ERR_INVALID_ARG; }
esp_err_t cooking_set_temperature(unsigned temperature_c)
{
    return temperature_c >= COOKER_TEMP_MIN_C && temperature_c <= COOKER_TEMP_MAX_C ?
           post(INTENT_SET_TEMP, temperature_c, false, NULL) : ESP_ERR_INVALID_ARG;
}
esp_err_t cooking_profile_select(unsigned index)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = select_profile_locked(index);
    xSemaphoreGive(s_lock);
    return err;
}
esp_err_t cooking_start(void)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = begin_run_locked();
    if (err != ESP_OK && s_status.state != COOK_STATE_FAULT) {
        strlcpy(s_status.detail, "START BLOCKED", sizeof(s_status.detail));
        sound_play(SOUND_WARNING);
        emit_status("start_blocked");
    }
    xSemaphoreGive(s_lock);
    return err;
}
esp_err_t cooking_stop(const char *reason) { return post(INTENT_STOP, 0, false, reason == NULL ? "USER STOP" : reason); }
esp_err_t cooking_pause_resume(void) { return post(INTENT_PAUSE_RESUME, 0, false, NULL); }
esp_err_t cooking_sleep(void) { return post(INTENT_SLEEP, 0, false, NULL); }
esp_err_t cooking_wake(void) { return post(INTENT_WAKE, 0, false, NULL); }
esp_err_t cooking_acknowledge(void) { return post(INTENT_ACK, 0, false, NULL); }
esp_err_t cooking_timer_set(uint32_t seconds, bool enabled) { return seconds <= COOKER_MAX_TIMER_S ? post(INTENT_TIMER_SET, seconds, enabled, NULL) : ESP_ERR_INVALID_ARG; }
esp_err_t cooking_timer_toggle(void) { return post(INTENT_TIMER_TOGGLE, 0, false, NULL); }
esp_err_t cooking_schedule_relative(uint32_t delay_s)
{
    if (delay_s < 1 || delay_s > 24U * 60U * 60U) return ESP_ERR_INVALID_ARG;
    cooker_snapshot_t snapshot;
    cooking_engine_get_snapshot(&snapshot);
    if (!state_schedulable(snapshot.state)) return ESP_ERR_INVALID_STATE;
    return post(INTENT_SCHEDULE_REL, delay_s, false, NULL);
}
esp_err_t cooking_schedule_absolute(int64_t epoch_s)
{
    const int64_t now = time(NULL);
    if (epoch_s <= now || epoch_s > now + 24 * 60 * 60) return ESP_ERR_INVALID_ARG;
    cooker_snapshot_t snapshot;
    cooking_engine_get_snapshot(&snapshot);
    if (!state_schedulable(snapshot.state)) return ESP_ERR_INVALID_STATE;
    if (!snapshot.clock_valid) return ESP_ERR_INVALID_STATE;
    return post(INTENT_SCHEDULE_ABS, epoch_s, false, NULL);
}
esp_err_t cooking_schedule_cancel(void) { return post(INTENT_SCHEDULE_CANCEL, 0, false, NULL); }
