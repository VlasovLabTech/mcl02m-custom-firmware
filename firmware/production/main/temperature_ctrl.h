#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"

typedef struct {
    temp_phase_t phase;
    float integral;
    uint32_t at_limit_ms;
    bool saturated;
    bool heat_enabled;
    uint8_t last_gear;
} temperature_ctrl_t;

void temperature_ctrl_reset(temperature_ctrl_t *controller);
uint8_t temperature_ctrl_update(temperature_ctrl_t *controller,
                                uint16_t target_c, uint8_t measured_c,
                                uint32_t elapsed_ms);
