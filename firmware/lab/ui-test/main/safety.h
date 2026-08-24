#pragma once

#include <stdbool.h>

/*
 * Deliberately a preprocessor constant, so a build cannot accidentally add
 * heating while retaining the UI-test name.
 */
#define MCL02M_HEAT_CONTROL_ENABLED 0

#if MCL02M_HEAT_CONTROL_ENABLED
#error "Heat control is forbidden in MCL02M UI test firmware"
#endif

enum {
    MCL02M_POWERBOARD_READ_MIN = 0x20,
    MCL02M_POWERBOARD_READ_MAX = 0x2f,
};

static inline bool mcl02m_powerboard_read_selector_allowed(unsigned reg)
{
    return reg >= MCL02M_POWERBOARD_READ_MIN &&
           reg <= MCL02M_POWERBOARD_READ_MAX;
}
