#include <assert.h>
#include <stdio.h>

#include "temperature_ctrl.h"

int main(void)
{
    temperature_ctrl_t c;
    temperature_ctrl_reset(&c);
    assert(temperature_ctrl_update(&c, 100, 20, 500) == 99);
    assert(temperature_ctrl_update(&c, 100, 75, 500) == 77);
    assert(temperature_ctrl_update(&c, 100, 89, 500) == 56);
    const unsigned approach = temperature_ctrl_update(&c, 100, 91, 500);
    assert(c.phase == TEMP_PHASE_APPROACH && approach == 26);
    assert(temperature_ctrl_update(&c, 100, 99, 500) == 6);
    assert(c.phase == TEMP_PHASE_HOLD);
    assert(temperature_ctrl_update(&c, 100, 100, 500) == 0);
    assert(temperature_ctrl_update(&c, 100, 101, 500) == 0);
    temperature_ctrl_reset(&c);
    assert(temperature_ctrl_update(&c, 58, 60, 500) == 0);
    assert(temperature_ctrl_update(&c, 58, 58, 500) == 0);
    assert(temperature_ctrl_update(&c, 58, 57, 500) == 6);
    for (unsigned i = 0; i < 400; ++i)
        assert(temperature_ctrl_update(&c, 100, 90, 500) <= 35);
    assert(c.saturated);
    puts("temperature_ctrl: PASS");
    return 0;
}
