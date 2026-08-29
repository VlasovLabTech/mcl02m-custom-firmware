#include "temperature_ctrl.h"

#include <string.h>

#include "app_config.h"

static uint8_t clamp_gear(int value, unsigned maximum)
{
    if (value < 0) return 0;
    if ((unsigned)value > maximum) return (uint8_t)maximum;
    return (uint8_t)value;
}

static void clear_control_state(temperature_ctrl_t *controller)
{
    controller->phase = TEMP_PHASE_PREHEAT;
    controller->integral = 0;
    controller->at_limit_ms = 0;
    controller->saturated = false;
    controller->last_gear = 0;
    controller->approach_exit_error_c = 0;
}

static uint8_t preheat_gear(int error)
{
    return error >= 30 ? 99 : (error >= 18 ? 77 : 56);
}

static uint8_t trend_rise(const temperature_ctrl_t *controller)
{
    if (controller->trend_count < TEMP_CTRL_TREND_SAMPLES) return 0;
    const unsigned latest = (controller->trend_next + TEMP_CTRL_TREND_SAMPLES - 1U) %
                            TEMP_CTRL_TREND_SAMPLES;
    const uint8_t oldest_c = controller->trend_samples[controller->trend_next];
    const uint8_t latest_c = controller->trend_samples[latest];
    return latest_c > oldest_c ? (uint8_t)(latest_c - oldest_c) : 0;
}

static uint8_t braking_margin(temperature_ctrl_t *controller, uint16_t target_c)
{
    controller->rise_4s_c = trend_rise(controller);
    unsigned margin = COOKER_TEMP_BRAKE_BASE_C + controller->rise_4s_c;
    if (margin > COOKER_TEMP_BRAKE_MAX_C) margin = COOKER_TEMP_BRAKE_MAX_C;
    if (target_c >= COOKER_TEMP_HIGH_TARGET_C &&
        margin < COOKER_TEMP_HIGH_BRAKE_MIN_C) {
        margin = COOKER_TEMP_HIGH_BRAKE_MIN_C;
    }
    controller->braking_margin_c = (uint8_t)margin;
    return controller->braking_margin_c;
}

void temperature_ctrl_reset(temperature_ctrl_t *controller)
{
    if (controller == NULL) return;
    memset(controller, 0, sizeof(*controller));
    clear_control_state(controller);
    controller->braking_margin_c = COOKER_TEMP_BRAKE_BASE_C;
}

void temperature_ctrl_restart(temperature_ctrl_t *controller)
{
    if (controller == NULL) return;
    clear_control_state(controller);
    controller->target_valid = false;
}

void temperature_ctrl_reset_trend(temperature_ctrl_t *controller)
{
    if (controller == NULL) return;
    memset(controller->trend_samples, 0, sizeof(controller->trend_samples));
    controller->trend_next = 0;
    controller->trend_count = 0;
    controller->rise_4s_c = 0;
    controller->braking_margin_c = COOKER_TEMP_BRAKE_BASE_C;
}

void temperature_ctrl_observe(temperature_ctrl_t *controller, uint8_t measured_c)
{
    if (controller == NULL) return;
    controller->trend_samples[controller->trend_next] = measured_c;
    controller->trend_next = (uint8_t)((controller->trend_next + 1U) %
                                       TEMP_CTRL_TREND_SAMPLES);
    if (controller->trend_count < TEMP_CTRL_TREND_SAMPLES) ++controller->trend_count;
}

uint8_t temperature_ctrl_update(temperature_ctrl_t *controller,
                                uint16_t target_c, uint8_t measured_c,
                                uint32_t elapsed_ms)
{
    if (controller == NULL) return 0;
    if (!controller->target_valid || controller->last_target_c != target_c) {
        clear_control_state(controller);
        controller->last_target_c = target_c;
        controller->target_valid = true;
    }
    const int error = (int)target_c - (int)measured_c;
    const uint8_t margin = braking_margin(controller, target_c);

    if (controller->phase == TEMP_PHASE_OFF) controller->phase = TEMP_PHASE_PREHEAT;

    /* At or above the setpoint, retain the session in active zero. */
    if (error <= 0) {
        controller->phase = TEMP_PHASE_HOLD;
        controller->integral = 0;
        controller->at_limit_ms = 0;
        controller->saturated = false;
        controller->last_gear = 0;
        return 0;
    }

    if (controller->phase == TEMP_PHASE_HOLD &&
        error >= (int)margin + COOKER_TEMP_PHASE_HYSTERESIS_C) {
        clear_control_state(controller);
    }

    if (controller->phase == TEMP_PHASE_PREHEAT) {
        if (error <= margin) {
            controller->phase = TEMP_PHASE_APPROACH;
            controller->integral = 0;
            controller->approach_exit_error_c =
                (uint8_t)(margin + COOKER_TEMP_PHASE_HYSTERESIS_C);
        } else {
            controller->last_gear = preheat_gear(error);
            return controller->last_gear;
        }
    }

    if (controller->phase == TEMP_PHASE_APPROACH) {
        if (error <= 2) {
            controller->phase = TEMP_PHASE_HOLD;
            controller->integral = 0;
        } else if (controller->approach_exit_error_c != 0 &&
                   error >= controller->approach_exit_error_c) {
            controller->phase = TEMP_PHASE_PREHEAT;
            controller->last_gear = preheat_gear(error);
            return controller->last_gear;
        } else {
            controller->at_limit_ms = 0;
            controller->saturated = false;
            controller->last_gear = clamp_gear(8 + error * 2, COOKER_HOLD_MAX_GEAR);
            return controller->last_gear;
        }
    }

    /* Stronger PI level, still capped below the first topology change. */
    const float dt = elapsed_ms / 1000.0f;
    const float candidate_integral = controller->integral + error * dt;
    float output = 4.0f + 2.0f * error + 0.08f * candidate_integral;
    if (output > COOKER_HOLD_MAX_GEAR) {
        output = COOKER_HOLD_MAX_GEAR;
    } else {
        controller->integral = candidate_integral;
    }
    if (output < 0) {
        output = 0;
        if (error < 0) controller->integral = candidate_integral;
    }
    controller->last_gear = clamp_gear((int)(output + 0.5f), COOKER_HOLD_MAX_GEAR);
    if (controller->last_gear == COOKER_HOLD_MAX_GEAR && error >= 3) {
        controller->at_limit_ms += elapsed_ms;
        if (controller->at_limit_ms >= COOKER_HOLD_SATURATED_MS)
            controller->saturated = true;
    } else {
        controller->at_limit_ms = 0;
        controller->saturated = false;
    }
    return controller->last_gear;
}
