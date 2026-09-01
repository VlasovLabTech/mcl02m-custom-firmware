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
#ifndef MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED
#define MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED 1
#endif
#ifndef MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES
#define MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES 1U
#endif
#ifndef MCL02M_IGBT_START_INHIBIT_C
#define MCL02M_IGBT_START_INHIBIT_C 0U
#endif
#ifndef MCL02M_MAX_BOTTOM_C
#define MCL02M_MAX_BOTTOM_C 120U
#endif
#ifndef MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES
#define MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES 1U
#endif
#ifndef MCL02M_RAW_SENSOR_FAULT_SAMPLES
#define MCL02M_RAW_SENSOR_FAULT_SAMPLES 1U
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
#ifndef MCL02M_LOW_START_RAMP_GEAR
#define MCL02M_LOW_START_RAMP_GEAR 10U
#endif
#ifndef MCL02M_LOW_TOPOLOGY_MAX_GEAR
#define MCL02M_LOW_TOPOLOGY_MAX_GEAR 35U
#endif
#ifndef MCL02M_MID_TOPOLOGY_MIN_GEAR
#define MCL02M_MID_TOPOLOGY_MIN_GEAR 36U
#endif
#ifndef MCL02M_SMALL_COOKWARE_MAX_GEAR
#define MCL02M_SMALL_COOKWARE_MAX_GEAR 35U
#endif
#ifndef MCL02M_HIGH_TOPOLOGY_MIN_GEAR
#define MCL02M_HIGH_TOPOLOGY_MIN_GEAR 56U
#endif
#ifndef MCL02M_START_CONFIRM_TIMEOUT_MS
#define MCL02M_START_CONFIRM_TIMEOUT_MS 8000U
#endif
#ifndef MCL02M_STOP_CONFIRM_TIMEOUT_MS
#define MCL02M_STOP_CONFIRM_TIMEOUT_MS 8000U
#endif
#ifndef MCL02M_STOP_CONFIRM_SAMPLES
#define MCL02M_STOP_CONFIRM_SAMPLES 2U
#endif
#ifndef MCL02M_COOKING_LEASE_ENABLED
#define MCL02M_COOKING_LEASE_ENABLED 0
#endif
#ifndef MCL02M_COOKING_LEASE_MS
#define MCL02M_COOKING_LEASE_MS 3000U
#endif
#ifndef MCL02M_TRANSITION_CONFIRM_TIMEOUT_MS
#define MCL02M_TRANSITION_CONFIRM_TIMEOUT_MS 3000U
#endif
#ifndef MCL02M_ARM_WINDOW_MS
#define MCL02M_ARM_WINDOW_MS 30000U
#endif
#ifndef MCL02M_I2C_RECOVERY_TRIGGER_CYCLES
#define MCL02M_I2C_RECOVERY_TRIGGER_CYCLES 3U
#endif
#ifndef MCL02M_I2C_RECOVERY_GOOD_CYCLES
#define MCL02M_I2C_RECOVERY_GOOD_CYCLES 2U
#endif
#ifndef MCL02M_I2C_RECOVERY_HEARTBEAT_MS
#define MCL02M_I2C_RECOVERY_HEARTBEAT_MS 320U
#endif
#ifndef MCL02M_I2C_CRITICAL_LOSS_TIMEOUT_MS
#define MCL02M_I2C_CRITICAL_LOSS_TIMEOUT_MS 5000U
#endif
#ifndef MCL02M_I2C_COMMAND_LOSS_TIMEOUT_MS
#define MCL02M_I2C_COMMAND_LOSS_TIMEOUT_MS 3000U
#endif
#ifndef MCL02M_KNOWN_R20_FAULT_SAMPLES
#define MCL02M_KNOWN_R20_FAULT_SAMPLES 2U
#endif
#ifndef MCL02M_NO_PAN_SAMPLES
#define MCL02M_NO_PAN_SAMPLES 3U
#endif

_Static_assert(MCL02M_SMALL_COOKWARE_MAX_GEAR <= MCL02M_LOW_TOPOLOGY_MAX_GEAR,
               "small cookware must stay on the stock low-power topology");
_Static_assert(MCL02M_LOW_START_RAMP_GEAR <= MCL02M_LOW_TOPOLOGY_MAX_GEAR,
               "low cold-start ramp must stay in the low topology");
_Static_assert(MCL02M_MID_TOPOLOGY_MIN_GEAR ==
               MCL02M_LOW_TOPOLOGY_MAX_GEAR + 1U,
               "middle topology must begin immediately after the low topology");
_Static_assert(MCL02M_HIGH_TOPOLOGY_MIN_GEAR > MCL02M_MID_TOPOLOGY_MIN_GEAR,
               "high topology must begin above the middle topology");
_Static_assert(MCL02M_COOKING_LEASE_MS >= 3U * MCL02M_CONTROL_HEARTBEAT_MS,
               "cooking lease must span at least three power heartbeats");
_Static_assert(MCL02M_I2C_RECOVERY_TRIGGER_CYCLES >= 2U,
               "I2C recovery must reject a single bad cycle");
_Static_assert(MCL02M_I2C_RECOVERY_GOOD_CYCLES >= 2U,
               "I2C recovery exit requires stable communication");
_Static_assert(MCL02M_I2C_RECOVERY_HEARTBEAT_MS < MCL02M_CONTROL_HEARTBEAT_MS,
               "I2C recovery polling must be faster than the normal heartbeat");
_Static_assert(MCL02M_I2C_COMMAND_LOSS_TIMEOUT_MS <=
               MCL02M_I2C_CRITICAL_LOSS_TIMEOUT_MS,
               "failed safety commands must not outlive a read-only outage");
_Static_assert(MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES >= 1U,
               "IGBT cutoff needs at least one sample");
_Static_assert(MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES >= 1U,
               "bottom cutoff needs at least one sample");
_Static_assert(MCL02M_RAW_SENSOR_FAULT_SAMPLES >= 1U,
               "raw sensor faults need at least one sample");

static inline bool mcl02m_powerboard_read_selector_allowed(unsigned reg)
{
    return reg >= 0x20U && reg <= 0x2fU;
}

static inline bool mcl02m_powerboard_control_register_allowed(unsigned reg)
{
    return reg == 0x0dU || reg == 0x00U || reg == 0x0cU;
}
