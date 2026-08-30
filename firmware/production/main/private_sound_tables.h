#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PRIVATE_SOUND_WAKE = 0,
    PRIVATE_SOUND_SLEEP,
} private_sound_table_t;

typedef struct {
    uint16_t frequency_hz;
    uint16_t on_ms;
    uint16_t gap_ms;
} private_sound_note_t;

bool private_sound_note(private_sound_table_t table, size_t index,
                        private_sound_note_t *note);
unsigned private_sound_duty_permille(void);
