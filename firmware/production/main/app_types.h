#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef enum { LANG_EN = 0, LANG_RU = 1, LANG_ZH = 2 } app_language_t;
typedef enum { TIMER_SCREEN_AUTO = 0, TIMER_SCREEN_ALWAYS = 1 } timer_screen_mode_t;
typedef enum { COOK_MODE_POWER = 0, COOK_MODE_TEMPERATURE = 1, COOK_MODE_PROFILE = 2 } cook_mode_t;

typedef enum {
    COOK_STATE_SLEEP = 0,
    COOK_STATE_IDLE,
    COOK_STATE_READY,
    COOK_STATE_DELAYED,
    COOK_STATE_STARTING,
    COOK_STATE_COOKING,
    COOK_STATE_PAUSED,
    COOK_STATE_NO_PAN,
    COOK_STATE_STOPPING,
    COOK_STATE_COMPLETE,
    COOK_STATE_FAULT,
} cook_state_t;

typedef enum {
    TEMP_PHASE_OFF = 0,
    TEMP_PHASE_PREHEAT,
    TEMP_PHASE_APPROACH,
    TEMP_PHASE_HOLD,
} temp_phase_t;

typedef enum {
    FAULT_NONE = 0,
    FAULT_E02_NO_PAN_TIMEOUT,
    FAULT_E03_HIGH_VOLTAGE,
    FAULT_E04_LOW_VOLTAGE,
    FAULT_E05_BOTTOM_OVERHEAT,
    FAULT_E07_IGBT_OVERHEAT,
    FAULT_E08_SENSOR,
    FAULT_E09_COMMUNICATION,
    FAULT_E10_WIRE_OR_CHANNEL,
    FAULT_E12_POWER_STATUS,
    FAULT_POWER_STATUS,
    FAULT_START_TIMEOUT,
    FAULT_COOKING_LEASE,
    FAULT_HARD_RUN_LIMIT,
} cooker_fault_t;

typedef struct {
    uint32_t schema;
    uint8_t language;
    uint8_t sound_enabled;
    uint8_t show_context_value;
    uint8_t show_igbt;
    uint8_t timer_screen_mode;
    uint8_t sleep_minutes;
    uint16_t oled_timeout_s;
    int16_t timezone_minutes;
    uint8_t show_sleep_clock;
    uint8_t wifi_enabled;
    uint8_t show_i2c_debug;
    uint8_t reserved[11];
    uint32_t crc32;
} app_settings_t;

typedef struct {
    uint8_t mode;
    uint8_t gear;
    uint16_t temperature_c;
    uint32_t timer_s;
} cooker_profile_stage_t;

typedef struct {
    char name[12];
    cooker_profile_stage_t stages[COOKER_PROFILE_STAGE_COUNT];
} cooker_profile_t;

typedef struct {
    cook_state_t state;
    cook_mode_t mode;
    temp_phase_t temp_phase;
    cooker_fault_t fault;
    uint8_t selected_gear;
    uint8_t applied_gear;
    uint8_t paused_gear;
    uint16_t target_temperature_c;
    uint8_t bottom_c;
    uint8_t igbt_c;
    uint8_t i2c_bad_cycles;
    uint16_t mains_voltage_v;
    bool readings_valid;
    uint8_t power_board_revision;
    bool power_board_revision_valid;
    bool pan_present;
    bool cookware_limited;
    uint32_t cookware_notice_seq;
    bool r20_warning_active;
    uint8_t r20_warning_value;
    uint32_t r20_warning_seq;
    bool active_zero;
    bool timer_enabled;
    uint32_t timer_remaining_s;
    uint32_t timer_last_s;
    bool delayed_start;
    bool delayed_absolute;
    int64_t delayed_epoch_s;
    uint32_t delayed_remaining_s;
    bool clock_valid;
    bool hold_saturated;
    uint32_t pause_remaining_s;
    uint32_t stop_elapsed_ms;
    uint32_t stop_generation;
    bool stop_timed_out;
    uint32_t lease_remaining_ms;
    uint32_t lease_generation;
    uint32_t lease_renewals;
    bool lease_active;
    bool lease_expired;
    uint32_t transition_remaining_ms;
    uint32_t transition_generation;
    uint32_t transition_confirmed_generation;
    uint32_t transition_rejection_sequence;
    uint8_t transition_kind;
    uint8_t transition_requested_gear;
    uint8_t transmitted_gear;
    uint8_t confirmed_gear;
    uint8_t feedback_r20;
    uint8_t feedback_r26;
    uint8_t feedback_gear;
    bool transition_pending;
    bool transition_command_transmitted;
    bool confirmation_inferred;
    char transition_result[24];
    char transition_rejection[32];
    uint32_t run_elapsed_s;
    uint32_t retained_session_remaining_s;
    uint32_t heating_elapsed_s;
    uint32_t active_zero_elapsed_s;
    uint32_t profile_zero_wait_elapsed_s;
    uint32_t manual_pause_elapsed_s;
    uint32_t no_pan_elapsed_s;
    uint8_t profile_index;
    uint8_t profile_stage_index;
    uint8_t profile_stage_count;
    uint8_t profile_stage_mode;
    char detail[32];
} cooker_snapshot_t;
