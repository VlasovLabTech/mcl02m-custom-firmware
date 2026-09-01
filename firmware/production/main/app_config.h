#pragma once

#include <stdint.h>

#define MCL02M_FIRMWARE_VERSION "0.2.34-dev"

#define COOKER_MAX_GEAR                 99U
#define COOKER_HOLD_MAX_GEAR            35U
#define COOKER_PREHEAT_MIN_GEAR         56U
#define COOKER_MAX_TIMER_S              (5U * 60U * 60U)
#define COOKER_RETAINED_SESSION_LIMIT_MS (8U * 60U * 60U * 1000U)
#define COOKER_MANUAL_PAUSE_TIMEOUT_MS   (2U * 60U * 60U * 1000U)
#define COOKER_NO_PAN_TIMEOUT_MS        128000U
#define COOKER_NO_PAN_START_FAILSAFE_MS  30000U
#define COOKER_NO_PAN_PLAY_FAILSAFE_MS  132000U
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
#define COOKER_TEMP_TREND_WINDOW_MS      4000U
#define COOKER_TEMP_BRAKE_BASE_C           10U
#define COOKER_TEMP_BRAKE_MAX_C            20U
#define COOKER_TEMP_HIGH_TARGET_C          170U
#define COOKER_TEMP_HIGH_BRAKE_MIN_C        15U
#define COOKER_TEMP_PHASE_HYSTERESIS_C       5U
#define COOKER_IMAGE_CONFIRM_MS         1500U
#define COOKER_IMAGE_WAKEUP_MS          3000U
#define COOKER_IMAGE_TURN_ON_MS         5000U
#define COOKER_IMAGE_COOKING_MS         2500U
#define COOKER_COMPLETE_NOTICE_MS      60000U
#define COOKER_SMALL_COOKWARE_NOTICE_MS 3000U
#define COOKER_IMAGE_CANCEL_MS          1500U
#define COOKER_IMAGE_SLEEP_WARNING_MS  10000U
#define COOKER_IMAGE_SLEEP_MS          10000U
#define COOKER_IMAGE_WIFI_PRESENT_MS    3000U
#define COOKER_HOT_THRESHOLD_C            60U
#define COOKER_HOT_IDLE_DELAY_MS         5000U
#define COOKER_HOT_BLINK_ON_MS           2000U
#define COOKER_HOT_BLINK_OFF_MS          1000U

/* Active-session advisory; the separately marked interface E07 is configured
 * in the power-board component and native R20=17 E07 remains independent. */
#define COOKER_IGBT_WARNING_C            92U
#define COOKER_IGBT_WARNING_CLEAR_C      92U
#define COOKER_IGBT_WARNING_SAMPLES       2U
#define COOKER_IGBT_WARNING_BEEP_MS     3000U
#define COOKER_IGBT_WARNING_RESHOW_MS   7000U

#define COOKER_DELAYED_START_ATTEMPTS      2U
#define COOKER_DELAYED_RETRY_WAIT_MS     1000U
#define COOKER_DELAYED_RETRY_DEADLINE_MS 5000U

#define COOKER_SETTINGS_SCHEMA           5U
/* Retain the temporary I2C-loss OLED implementation, but omit it from production. */
#define COOKER_I2C_DEBUG_DISPLAY_ENABLED  0U
#define COOKER_I2C_DEBUG_MAX             6U
#define COOKER_I2C_DEBUG_HOLD_MS       2000U
#define COOKER_PROFILE_COUNT             5U
#define COOKER_PROFILE_STAGE_COUNT       5U
