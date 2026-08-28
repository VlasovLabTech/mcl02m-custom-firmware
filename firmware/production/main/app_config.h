#pragma once

#include <stdint.h>

#define MCL02M_FIRMWARE_VERSION "0.2.7-dev"

#define COOKER_MAX_GEAR                 99U
#define COOKER_HOLD_MAX_GEAR            35U
#define COOKER_PREHEAT_MIN_GEAR         56U
#define COOKER_MAX_TIMER_S              (5U * 60U * 60U)
#define COOKER_HARD_RUN_LIMIT_MS         (5U * 60U * 60U * 1000U)
#define COOKER_MANUAL_PAUSE_TIMEOUT_MS   (2U * 60U * 60U * 1000U)
#define COOKER_NO_PAN_TIMEOUT_MS         60000U
#define COOKER_NO_PAN_SOUND_PAUSE_MS      3000U
#define COOKER_POWERBOARD_ARM_MS         30000U
#define COOKER_ENCODER_WAKE_GUARD_MS     1500U
#define COOKER_DEFAULT_SLEEP_MIN         1U
#define COOKER_DEFAULT_OLED_TIMEOUT_S    180U
#define COOKER_HOLD_SATURATED_MS         90000U
#define COOKER_TEMP_MIN_C                40U
#define COOKER_TEMP_MAX_C                190U
#define COOKER_TEMP_STEP_C               1U
#define COOKER_CONTROL_PERIOD_MS         100U
#define COOKER_TEMP_UPDATE_MS            500U
#define COOKER_IMAGE_CONFIRM_MS         1500U
#define COOKER_IMAGE_WAKEUP_MS          3000U
#define COOKER_IMAGE_TURN_ON_MS         5000U
#define COOKER_IMAGE_COOKING_MS         2500U
#define COOKER_IMAGE_CANCEL_MS          1500U
#define COOKER_IMAGE_SLEEP_WARNING_MS  10000U
#define COOKER_IMAGE_SLEEP_MS          10000U

/* Additional interface-side guard; the power MCU retains its own protections. */
#define COOKER_IGBT_LIMIT_C              80U

#define COOKER_SETTINGS_SCHEMA           5U
#define COOKER_I2C_DEBUG_MAX             6U
#define COOKER_PROFILE_COUNT             5U
#define COOKER_PROFILE_STAGE_COUNT       5U
