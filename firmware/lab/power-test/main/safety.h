#pragma once

#include <stdbool.h>

#ifndef MCL02M_POWER_TEST_BUILD
#define MCL02M_POWER_TEST_BUILD 1
#endif

/* Hard limits for the dedicated bring-up image, not production defaults. */
#ifndef MCL02M_MAX_GEAR
#define MCL02M_MAX_GEAR 99U
#endif
#ifndef MCL02M_MAX_RUN_MS
#define MCL02M_MAX_RUN_MS 300000U
#endif
#ifndef MCL02M_MAX_IGBT_C
#define MCL02M_MAX_IGBT_C 80U
#endif
#ifndef MCL02M_MAX_BOTTOM_C
#define MCL02M_MAX_BOTTOM_C 120U
#endif
#ifndef MCL02M_MAX_HEARTBEAT_GAP_MS
#define MCL02M_MAX_HEARTBEAT_GAP_MS 5000U
#endif
#ifndef MCL02M_CONTROL_HEARTBEAT_MS
#define MCL02M_CONTROL_HEARTBEAT_MS 500U
#endif
#ifndef MCL02M_GEAR_STEP_PER_HEARTBEAT
#define MCL02M_GEAR_STEP_PER_HEARTBEAT 10U
#endif
#ifndef MCL02M_START_CONFIRM_TIMEOUT_MS
#define MCL02M_START_CONFIRM_TIMEOUT_MS 8000U
#endif
#ifndef MCL02M_ARM_WINDOW_MS
#define MCL02M_ARM_WINDOW_MS 30000U
#endif
#ifndef MCL02M_I2C_BAD_CYCLES_TO_FAULT
#define MCL02M_I2C_BAD_CYCLES_TO_FAULT 2U
#endif
#ifndef MCL02M_UNKNOWN_STATUS_SAMPLES_TO_FAULT
#define MCL02M_UNKNOWN_STATUS_SAMPLES_TO_FAULT 2U
#endif
#ifndef MCL02M_R20_TRANSITION_MAX_SAMPLES
#define MCL02M_R20_TRANSITION_MAX_SAMPLES 20U
#endif
#ifndef MCL02M_NO_PAN_SAMPLES
#define MCL02M_NO_PAN_SAMPLES 3U
#endif

static inline bool mcl02m_powerboard_read_selector_allowed(unsigned reg)
{
    return reg >= 0x20U && reg <= 0x2fU;
}

static inline bool mcl02m_powerboard_control_register_allowed(unsigned reg)
{
    return reg == 0x0dU || reg == 0x00U || reg == 0x0cU;
}
