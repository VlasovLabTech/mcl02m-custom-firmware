#include <assert.h>
#include <stdio.h>

#include "temperature_ctrl.h"

static unsigned step(temperature_ctrl_t *controller, unsigned target, unsigned measured)
{
    temperature_ctrl_observe(controller, (uint8_t)measured);
    return temperature_ctrl_update(controller, (uint16_t)target, (uint8_t)measured, 500);
}

static void observe_many(temperature_ctrl_t *controller, const uint8_t *samples,
                         unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        temperature_ctrl_observe(controller, samples[i]);
}

int main(void)
{
    temperature_ctrl_t c;
    temperature_ctrl_reset(&c);
    assert(step(&c, 100, 20) == 99);
    assert(step(&c, 100, 75) == 77);
    assert(step(&c, 100, 89) == 56);
    const unsigned approach = step(&c, 100, 91);
    assert(c.phase == TEMP_PHASE_APPROACH && approach == 26);
    assert(step(&c, 100, 99) == 6);
    assert(c.phase == TEMP_PHASE_HOLD);
    assert(step(&c, 100, 100) == 0);
    assert(step(&c, 100, 101) == 0);

    temperature_ctrl_reset(&c);
    assert(step(&c, 58, 60) == 0);
    assert(step(&c, 58, 58) == 0);
    assert(step(&c, 58, 57) == 6);

    /* A five-degree rise over four seconds starts braking 15 C early. */
    temperature_ctrl_reset(&c);
    const uint8_t fast_rise[] = {105, 106, 106, 107, 107, 108, 109, 109, 110};
    observe_many(&c, fast_rise, sizeof(fast_rise));
    assert(temperature_ctrl_update(&c, 125, 110, 500) == 35);
    assert(c.phase == TEMP_PHASE_APPROACH);
    assert(c.rise_4s_c == 5 && c.braking_margin_c == 15);
    assert(c.approach_exit_error_c == 20);
    /* The entry margin is latched for five-degree phase hysteresis. */
    assert(step(&c, 125, 107) == 35);
    assert(c.phase == TEMP_PHASE_APPROACH);
    assert(step(&c, 125, 105) == 77);
    assert(c.phase == TEMP_PHASE_PREHEAT);

    /* A slow load keeps most of the original fast-heating interval. */
    temperature_ctrl_reset(&c);
    const uint8_t slow_rise[] = {108, 108, 108, 109, 109, 109, 110, 110, 110};
    observe_many(&c, slow_rise, sizeof(slow_rise));
    assert(temperature_ctrl_update(&c, 125, 110, 500) == 56);
    assert(c.rise_4s_c == 2 && c.braking_margin_c == 12);

    /* High targets always retain at least 15 C of braking reserve. */
    temperature_ctrl_reset(&c);
    const uint8_t flat_high[] = {175, 175, 175, 175, 175, 175, 175, 175, 175};
    observe_many(&c, flat_high, sizeof(flat_high));
    assert(temperature_ctrl_update(&c, 190, 175, 500) == 35);
    assert(c.braking_margin_c == 15);

    /* Resume preserves trend but clears PI memory and recomputes from the setpoint. */
    temperature_ctrl_reset(&c);
    assert(step(&c, 125, 125) == 0);
    unsigned near_target = 0;
    for (unsigned i = 0; i < 20; ++i) near_target = step(&c, 125, 124);
    assert(near_target == 7);
    const uint8_t history_count = c.trend_count;
    temperature_ctrl_restart(&c);
    assert(c.integral == 0 && c.trend_count == history_count);
    assert(temperature_ctrl_update(&c, 125, 124, 500) == 6);

    /* A sensor-data gap invalidates only the time-based trend evidence. */
    const temp_phase_t phase_before_gap = c.phase;
    const float integral_before_gap = c.integral;
    const uint16_t target_before_gap = c.last_target_c;
    temperature_ctrl_reset_trend(&c);
    assert(c.trend_count == 0 && c.trend_next == 0 && c.rise_4s_c == 0);
    assert(c.braking_margin_c == COOKER_TEMP_BRAKE_BASE_C);
    assert(c.phase == phase_before_gap && c.integral == integral_before_gap);
    assert(c.target_valid && c.last_target_c == target_before_gap);
    temperature_ctrl_observe(&c, 124);
    assert(c.trend_count == 1);
    assert(temperature_ctrl_update(&c, 125, 124, 500) == 6);

    /* A changed target starts a fresh phase decision instead of staying in HOLD. */
    assert(step(&c, 160, 124) == 99);
    assert(c.phase == TEMP_PHASE_PREHEAT);

    temperature_ctrl_reset(&c);
    assert(step(&c, 100, 99) == 6);
    for (unsigned i = 0; i < 400; ++i)
        assert(step(&c, 100, 90) <= 35);
    assert(c.saturated);
    puts("temperature_ctrl: PASS");
    return 0;
}
