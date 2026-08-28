#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "app_types.h"

#define TEMP_CTRL_TREND_SAMPLES \
    (COOKER_TEMP_TREND_WINDOW_MS / COOKER_TEMP_UPDATE_MS + 1U)

typedef struct {
    temp_phase_t phase;
    float integral;
    uint32_t at_limit_ms;
    bool saturated;
    uint8_t last_gear;
    uint8_t trend_samples[TEMP_CTRL_TREND_SAMPLES];
    uint8_t trend_next;
    uint8_t trend_count;
    uint8_t rise_4s_c;
    uint8_t braking_margin_c;
    uint8_t approach_exit_error_c;
    uint16_t last_target_c;
    bool target_valid;
} temperature_ctrl_t;

void temperature_ctrl_reset(temperature_ctrl_t *controller);
void temperature_ctrl_restart(temperature_ctrl_t *controller);
void temperature_ctrl_observe(temperature_ctrl_t *controller, uint8_t measured_c);
uint8_t temperature_ctrl_update(temperature_ctrl_t *controller,
                                uint16_t target_c, uint8_t measured_c,
                                uint32_t elapsed_ms);
