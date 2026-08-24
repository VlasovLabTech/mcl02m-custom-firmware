#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    UI_INPUT_ENCODER = 0,
    UI_INPUT_MAIN_PRESSED,
    UI_INPUT_MAIN_LONG,
    UI_INPUT_MAIN_RELEASED,
    UI_INPUT_TOUCH_A_PRESSED,
    UI_INPUT_TOUCH_A_RELEASED,
    UI_INPUT_TOUCH_B_PRESSED,
    UI_INPUT_TOUCH_B_RELEASED,
    UI_INPUT_TOUCH_BOTH_PRESSED,
    UI_INPUT_TOUCH_BOTH_RELEASED,
} ui_input_type_t;

typedef struct {
    ui_input_type_t type;
    int32_t value;
    uint32_t duration_ms;
    int64_t timestamp_ms;
} ui_input_event_t;

esp_err_t ui_inputs_init(void);
bool ui_inputs_get_event(ui_input_event_t *event, TickType_t wait_ticks);
