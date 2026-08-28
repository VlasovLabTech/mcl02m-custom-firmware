#include "temperature_ctrl.h"

#include <string.h>

#include "app_config.h"

static uint8_t clamp_gear(int value, unsigned maximum)
{
    if (value < 0) return 0;
    if ((unsigned)value > maximum) return (uint8_t)maximum;
    return (uint8_t)value;
}

void temperature_ctrl_reset(temperature_ctrl_t *controller)
{
    if (controller == NULL) return;
    memset(controller, 0, sizeof(*controller));
    controller->phase = TEMP_PHASE_PREHEAT;
}

uint8_t temperature_ctrl_update(temperature_ctrl_t *controller,
                                uint16_t target_c, uint8_t measured_c,
                                uint32_t elapsed_ms)
{
    if (controller == NULL) return 0;
    const int error = (int)target_c - (int)measured_c;

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

    if (controller->phase == TEMP_PHASE_PREHEAT) {
        if (error <= 10) {
            controller->phase = TEMP_PHASE_APPROACH;
            controller->integral = 0;
        } else {
            controller->last_gear = error >= 30 ? 99 : (error >= 18 ? 77 : 56);
            return controller->last_gear;
        }
    }

    if (controller->phase == TEMP_PHASE_APPROACH) {
        if (error <= 2) {
            controller->phase = TEMP_PHASE_HOLD;
            controller->integral = 0;
        } else if (error >= 14) {
            controller->phase = TEMP_PHASE_PREHEAT;
            controller->last_gear = 56;
            return controller->last_gear;
        } else {
            /* Restore the proven stronger approach while remaining below gear 36. */
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
