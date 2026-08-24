#pragma once

#include "driver/gpio.h"

enum {
    PIN_ENCODER_PHASE_0 = GPIO_NUM_5,
    PIN_ENCODER_PHASE_1 = GPIO_NUM_14,
    PIN_MAIN_BUTTON     = GPIO_NUM_34,

    PIN_TOUCH_SDA       = GPIO_NUM_19,
    PIN_TOUCH_SCL       = GPIO_NUM_18,

    PIN_LED_STB         = GPIO_NUM_17,
    PIN_LED_CLK         = GPIO_NUM_16,
    PIN_LED_DATA        = GPIO_NUM_4,

    PIN_UI_DIRECT_0     = GPIO_NUM_22,
    PIN_UI_DIRECT_1     = GPIO_NUM_32,

    PIN_BUZZER          = GPIO_NUM_23,

    PIN_OLED_SCLK       = GPIO_NUM_25,
    PIN_OLED_MOSI       = GPIO_NUM_27,
    PIN_OLED_RESET      = GPIO_NUM_26,
    PIN_OLED_DC         = GPIO_NUM_33,

    PIN_POWERBOARD_SDA  = GPIO_NUM_13,
    PIN_POWERBOARD_SCL  = GPIO_NUM_15,
};
