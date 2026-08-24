#!/usr/bin/env python3
"""Generate the strongly event-directed MCL02M chiptune sound pack v4."""

from __future__ import annotations

from pathlib import Path

from generate_previews import (
    Melody,
    Note,
    melody_pcm,
    milliseconds_to_samples,
    write_c_tables,
    write_wav,
)


# Equal-tempered note frequencies rounded to integer hertz.
C5, D5, E5, F5, G5, A5 = 523, 587, 659, 698, 784, 880
C7, CS7, D7, E7, F7, FS7, G7, GS7, A7, B7 = (
    2093, 2217, 2349, 2637, 2794, 2960, 3136, 3322, 3520, 3951,
)
C8, CS8, D8, DS8, E8 = 4186, 4435, 4699, 4978, 5274


MELODIES_V4 = (
    Melody(
        c_name="boot_v4_grand_fanfare",
        wav_name="01_power_on_v4_grand_fanfare.wav",
        title="Power on v4 / Grand 8-bit fanfare",
        purpose="ceremonial title-screen opening with a clear READY cadence",
        notes=(
            Note(C7, 120, 40), Note(C7, 120, 40), Note(G7, 300, 100),
            Note(E7, 120, 40), Note(G7, 120, 40), Note(C8, 360, 120),
            Note(F7, 100, 25), Note(A7, 100, 25), Note(C8, 220, 80),
            Note(G7, 100, 25), Note(B7, 100, 25), Note(D8, 220, 100),
            Note(C8, 140, 30), Note(G7, 140, 30), Note(E7, 140, 30), Note(C8, 650, 150),
            Note(C7, 80, 20), Note(E7, 80, 20), Note(G7, 80, 20),
            Note(C8, 80, 20), Note(E8, 500),
        ),
    ),
    Melody(
        c_name="complete_v4_saints",
        wav_name="02_cooking_complete_v4_when_the_saints.wav",
        title="Cooking complete v4 / When the Saints",
        purpose="bright, jubilant and encouraging finish with an arcade high note",
        notes=(
            # Traditional public-domain melody, newly timed as a chiptune.
            Note(C7, 120, 30), Note(E7, 120, 30), Note(F7, 120, 30), Note(G7, 300, 80),
            Note(C7, 120, 30), Note(E7, 120, 30), Note(F7, 120, 30), Note(G7, 300, 80),
            Note(C7, 120, 30), Note(E7, 120, 30), Note(F7, 120, 30), Note(G7, 200, 30),
            Note(E7, 120, 30), Note(C7, 120, 30), Note(E7, 140, 30), Note(D7, 260, 80),
            Note(E7, 100, 20), Note(D7, 100, 20), Note(C7, 140, 40),
            Note(E7, 100, 20), Note(G7, 100, 20), Note(C8, 480),
        ),
    ),
    Melody(
        c_name="no_pan_v4_sharp_alarm",
        wav_name="03_no_pan_v4_sharp_alarm.wav",
        title="No pan v4 / Sharp low-health alarm",
        purpose="unresolved tritone siren and chromatic fall; urgent but shorter than critical",
        notes=(
            Note(C8, 110, 35), Note(FS7, 110, 35),
            Note(C8, 110, 35), Note(FS7, 110, 35),
            Note(C8, 110, 35), Note(FS7, 110, 35),
            Note(D8, 110, 35), Note(GS7, 110, 35),
            Note(D8, 110, 35), Note(GS7, 110, 35),
            Note(D8, 110, 35), Note(GS7, 110, 35),
            Note(E8, 80, 20), Note(DS8, 80, 20), Note(D8, 80, 20),
            Note(CS8, 80, 20), Note(C8, 80, 20), Note(B7, 80, 20),
            Note(FS7, 400),
        ),
    ),
    Melody(
        c_name="critical_v4_bach_toccata",
        wav_name="04_critical_error_v4_bach_toccata.wav",
        title="Critical error v4 / Bach Toccata BWV 565",
        purpose="the famous organ opening reduced to a severe monophonic PWM gesture",
        notes=(
            # BWV 565 opening: A-G-A, then the characteristic fall to D.
            Note(A7, 110, 30), Note(G7, 110, 30), Note(A7, 180, 60),
            Note(G7, 110, 30), Note(F7, 110, 30), Note(E7, 110, 30),
            Note(D7, 110, 30), Note(CS7, 110, 30), Note(D7, 420, 120),
            # Dramatic answering gesture and octave-like final impact.
            Note(A7, 120, 30), Note(E7, 120, 30), Note(F7, 120, 30),
            Note(CS7, 120, 30), Note(D7, 450, 120), Note(D8, 650),
        ),
    ),
    Melody(
        c_name="sleep_v4_twinkle",
        wav_name="05_sleep_v4_twinkle_lullaby.wav",
        title="Sleep v4 / Low-register starlight lullaby",
        purpose="quiet-feeling, spacious and peaceful familiar bedtime phrase",
        notes=(
            # Ah! vous dirai-je, maman / Twinkle Twinkle in a low buzzer register.
            Note(C5, 260, 60), Note(C5, 260, 60),
            Note(G5, 260, 60), Note(G5, 260, 60),
            Note(A5, 260, 60), Note(A5, 260, 60), Note(G5, 500, 100),
            Note(F5, 260, 60), Note(F5, 260, 60),
            Note(E5, 260, 60), Note(E5, 260, 60),
            Note(D5, 260, 60), Note(D5, 260, 60), Note(C5, 500),
        ),
    ),
)


def main() -> int:
    root = Path(__file__).resolve().parent
    preview_dir = root / "previews_v4"
    combined: list[int] = []
    separator = [0] * milliseconds_to_samples(1500)

    for index, melody in enumerate(MELODIES_V4):
        pcm = melody_pcm(melody)
        if melody.c_name == "sleep_v4_twinkle":
            # Preview the quieter character intended for a future 15-20% PWM
            # duty implementation.  The generated note table remains PCM-free.
            pcm = [round(sample * 0.5) for sample in pcm]
        write_wav(preview_dir / melody.wav_name, pcm)
        if index:
            combined.extend(separator)
        combined.extend(pcm)
        print(
            f"{melody.wav_name}: {melody.duration_ms / 1000:.3f} s, "
            f"{len(melody.notes)} notes"
        )

    write_wav(preview_dir / "00_all_v4_in_order.wav", combined)
    write_c_tables(root / "melody_tables_v4.generated.h", MELODIES_V4)
    print(f"Generated v4 previews in {preview_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
