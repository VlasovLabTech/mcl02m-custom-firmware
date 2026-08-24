#!/usr/bin/env python3
"""Build the user-selected MCL02M buzzer pack plus a wake-up sound."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path

from generate_previews import (
    Melody,
    Note,
    melody_pcm,
    milliseconds_to_samples,
    write_c_tables,
    write_wav,
)
from generate_previews_v3 import MELODIES_V3
from generate_previews_v4 import MELODIES_V4


C7, E7, G7, C8, E8 = 2093, 2637, 3136, 4186, 5274


WAKE = Melody(
    c_name="wake",
    wav_name="06_wake.wav",
    title="Wake / Simple bright arpeggio",
    purpose="short, cheerful and immediately distinct from the longer boot fanfare",
    notes=(
        Note(C7, 160, 40), Note(E7, 160, 40), Note(G7, 160, 40),
        Note(C8, 240, 100),
        Note(G7, 100, 30), Note(C8, 100, 30), Note(E8, 650),
    ),
)


SELECTED = (
    replace(
        MELODIES_V3[0],
        c_name="boot",
        wav_name="01_power_on_korobeiniki.wav",
        title="Power on / Korobeiniki",
    ),
    replace(
        MELODIES_V4[1],
        c_name="complete",
        wav_name="02_cooking_complete_when_the_saints.wav",
        title="Cooking complete / When the Saints",
    ),
    replace(
        MELODIES_V4[2],
        c_name="no_pan",
        wav_name="03_no_pan_sharp_alarm.wav",
        title="No pan / Sharp alarm",
    ),
    replace(
        MELODIES_V3[3],
        c_name="critical",
        wav_name="04_critical_beethoven_fifth.wav",
        title="Critical / Beethoven Fifth",
    ),
    replace(
        MELODIES_V4[4],
        c_name="sleep",
        wav_name="05_sleep_twinkle.wav",
        title="Sleep / Low-register Twinkle",
    ),
    WAKE,
)


def main() -> int:
    root = Path(__file__).resolve().parent
    preview_dir = root / "previews_selected"
    combined: list[int] = []
    separator = [0] * milliseconds_to_samples(1500)

    for index, melody in enumerate(SELECTED):
        pcm = melody_pcm(melody)
        if melody.c_name == "sleep":
            # Approximate the intended future 15-20% PWM duty in the PC preview.
            pcm = [round(sample * 0.5) for sample in pcm]
        write_wav(preview_dir / melody.wav_name, pcm)
        if index:
            combined.extend(separator)
        combined.extend(pcm)
        print(
            f"{melody.wav_name}: {melody.duration_ms / 1000:.3f} s, "
            f"{len(melody.notes)} notes"
        )

    write_wav(preview_dir / "00_selected_pack_in_order.wav", combined)
    table_path = root / "melody_tables_selected.generated.h"
    write_c_tables(table_path, SELECTED)
    with table_path.open("a", encoding="utf-8", newline="\n") as tables:
        tables.write(
            "\n/* Integration targets: normal sounds 50%, sleep about 18% PWM duty. */\n"
            "#define SELECTED_SOUND_NORMAL_DUTY_PERMILLE 500U\n"
            "#define SELECTED_SOUND_SLEEP_DUTY_PERMILLE  180U\n"
        )
    print(f"Generated selected pack in {preview_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
