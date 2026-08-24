#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    SOUND_UI_CLICK = 0,
    SOUND_BOOT,
    SOUND_WAKE,
    SOUND_SLEEP,
    SOUND_COMPLETE,
    SOUND_STAGE,
    SOUND_WARNING,
    SOUND_NO_PAN,
    SOUND_CRITICAL,
} sound_pattern_t;

esp_err_t sound_init(void);
void sound_set_enabled(bool enabled);
void sound_play(sound_pattern_t pattern);
void sound_stop(void);
