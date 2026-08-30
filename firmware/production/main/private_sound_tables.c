#include "private_sound_tables.h"

#include "melody_tables_midi.generated.h"

bool private_sound_note(private_sound_table_t table, size_t index,
                        private_sound_note_t *note)
{
    if (note == NULL) return false;
    const buzzer_note_t *source = table == PRIVATE_SOUND_WAKE ?
                                  k_sound_midi_lce : k_sound_midi_snm;
    note->frequency_hz = source[index].frequency_hz;
    note->on_ms = source[index].on_ms;
    note->gap_ms = source[index].gap_ms;
    return note->frequency_hz != 0 || note->on_ms != 0 || note->gap_ms != 0;
}

unsigned private_sound_duty_permille(void)
{
    return MIDI_SOUND_NORMAL_DUTY_PERMILLE;
}
