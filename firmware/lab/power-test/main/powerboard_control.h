#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    PB_STATE_BOOT = 0,
    PB_STATE_STOPPED,
    PB_STATE_ARMED,
    PB_STATE_STARTING,
    PB_STATE_HEATING,
    PB_STATE_ACTIVE_ZERO,
    PB_STATE_PAUSED,
    PB_STATE_NO_PAN,
    PB_STATE_HEARTBEAT_GAP,
    PB_STATE_FAULT,
} powerboard_state_t;

typedef struct {
    powerboard_state_t state;
    uint8_t target_gear;
    uint8_t applied_gear;
    uint8_t topology;
    uint8_t last_command_0d;
    uint8_t last_command_00;
    uint8_t last_command_0c;
    uint8_t registers[16];
    uint16_t valid_mask;
    uint8_t igbt_c;
    uint8_t bottom_c;
    uint32_t run_elapsed_ms;
    uint32_t run_remaining_ms;
    uint32_t arm_remaining_ms;
    uint32_t start_confirm_remaining_ms;
    uint32_t heartbeat_gap_remaining_ms;
    uint32_t completed_cycles;
    uint32_t bad_cycles;
    uint32_t consecutive_bad_cycles;
    uint32_t active_zero_entries;
    uint32_t active_zero_resumes;
    uint32_t unknown_r20_seq;
    uint8_t unknown_r20_value;
    bool cookware_limited;
    bool stop_verified;
    bool heartbeat_gap_observed_stop;
    char fault[24];
} powerboard_status_t;

esp_err_t powerboard_control_init(void);
void powerboard_control_get_status(powerboard_status_t *status);
size_t powerboard_control_status_json(char *output, size_t output_size);

esp_err_t powerboard_control_arm(unsigned window_ms);
esp_err_t powerboard_control_start(unsigned gear, unsigned duration_ms);
esp_err_t powerboard_control_set_gear(unsigned gear);
esp_err_t powerboard_control_pause(void);
esp_err_t powerboard_control_resume(void);
esp_err_t powerboard_control_stop(const char *reason);
esp_err_t powerboard_control_heartbeat_gap(unsigned duration_ms);
esp_err_t powerboard_control_clear_fault(void);

const char *powerboard_state_name(powerboard_state_t state);
