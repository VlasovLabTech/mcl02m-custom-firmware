#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ui_controller_init(void);
bool ui_controller_timer_editing(void);
bool ui_controller_setpoint_editing(void);
