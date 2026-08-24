#!/usr/bin/env python3
"""Generate the more game-like MCL02M chiptune sound pack v2."""

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


# Equal-tempered notes, rounded to integer hertz.  The pack deliberately uses
# fast monophonic arpeggios: the passive PWM buzzer never needs PCM playback or
# polyphony.  Frequencies remain below 5.3 kHz and avoid the reported 6-8 kHz dip.
G5 = 784
C6, CS6, E6, FS6, G6, A6, AS6, B6 = 1047, 1109, 1319, 1480, 1568, 1760, 1865, 1976
C7, CS7, D7, E7, FS7, G7, A7, AS7, B7 = 2093, 2217, 2349, 2637, 2960, 3136, 3520, 3729, 3951
C8, D8, E8 = 4186, 4699, 5274


MELODIES_V2 = (
    Melody(
        c_name="boot_v2",
        wav_name="01_power_on_v2.wav",
        title="Power on v2 / New game",
        purpose="three arpeggiated power-up chords and a level-start flourish",
        notes=(
            Note(C6, 80, 20), Note(E6, 80, 20), Note(G6, 80, 20), Note(C7, 140, 60),
            Note(E6, 80, 20), Note(G6, 80, 20), Note(C7, 80, 20), Note(E7, 140, 60),
            Note(G6, 80, 20), Note(C7, 80, 20), Note(E7, 80, 20), Note(G7, 160, 80),
            Note(E7, 90, 25), Note(G7, 90, 25), Note(A7, 150, 45),
            Note(G7, 90, 25), Note(E7, 90, 25), Note(D7, 160, 80),
            Note(C7, 75, 15), Note(D7, 75, 15), Note(E7, 75, 15),
            Note(G7, 75, 15), Note(A7, 75, 15), Note(B7, 75, 15),
            Note(C8, 180, 40), Note(E8, 380),
        ),
    ),
    Melody(
        c_name="complete_v2",
        wav_name="02_cooking_complete_v2.wav",
        title="Cooking complete v2 / Stage clear",
        purpose="syncopated victory fanfare with a bright two-note finish",
        notes=(
            Note(C7, 100, 25), Note(C7, 100, 25), Note(E7, 130, 30), Note(G7, 160, 60),
            Note(E7, 90, 20), Note(G7, 90, 20), Note(C8, 200, 80),
            Note(A7, 90, 20), Note(B7, 90, 20), Note(C8, 90, 20), Note(E8, 220, 80),
            Note(E8, 80, 20), Note(D8, 80, 20), Note(C8, 120, 40), Note(G7, 140, 60),
            Note(C8, 100, 20), Note(E8, 100, 20), Note(C8, 100, 20), Note(E8, 450),
        ),
    ),
    Melody(
        c_name="no_pan_v2",
        wav_name="03_no_pan_v2.wav",
        title="No pan v2 / Lost item",
        purpose="quirky falling phrases followed by an audible question mark",
        notes=(
            Note(G7, 90, 25), Note(E7, 90, 25), Note(C7, 160, 100),
            Note(G7, 90, 25), Note(E7, 90, 25), Note(CS7, 160, 180),
            Note(C7, 100, 20), Note(B6, 100, 20), Note(AS6, 100, 20), Note(A6, 200, 180),
            Note(D7, 100, 30), Note(G7, 180, 80), Note(C7, 260),
        ),
    ),
    Melody(
        c_name="critical_motif_v2",
        wav_name="04_critical_error_motif_v2.wav",
        title="Critical error v2 / Boss alarm",
        purpose="tritone alarm pulses, chromatic drop and a loud final hold",
        notes=(
            Note(C8, 130, 50), Note(FS7, 130, 50),
            Note(C8, 130, 50), Note(FS7, 130, 50),
            Note(C8, 130, 50), Note(FS7, 130, 50),
            Note(C8, 130, 50), Note(FS7, 130, 50),
            Note(D8, 100, 40), Note(C8, 100, 40), Note(B7, 100, 40), Note(AS7, 100, 40),
            Note(4000, 500),
        ),
    ),
    Melody(
        c_name="sleep_v2",
        wav_name="05_sleep_v2.wav",
        title="Sleep v2 / Save and quit",
        purpose="descending arpeggios that progressively slow down",
        notes=(
            Note(E8, 80, 20), Note(C8, 80, 20), Note(G7, 100, 30), Note(E7, 140, 70),
            Note(C8, 90, 25), Note(G7, 90, 25), Note(E7, 120, 40), Note(C7, 180, 100),
            Note(G7, 100, 30), Note(E7, 120, 40), Note(C7, 160, 80), Note(G6, 240, 150),
            Note(E7, 120, 40), Note(C7, 160, 70), Note(G6, 220, 120), Note(E6, 320, 220),
            Note(C6, 420, 220), Note(G5, 650),
        ),
    ),
)


def main() -> int:
    root = Path(__file__).resolve().parent
    preview_dir = root / "previews_v2"
    combined: list[int] = []
    separator = [0] * milliseconds_to_samples(1500)

    for index, melody in enumerate(MELODIES_V2):
        pcm = melody_pcm(melody)
        write_wav(preview_dir / melody.wav_name, pcm)
        if index:
            combined.extend(separator)
        combined.extend(pcm)
        print(
            f"{melody.wav_name}: {melody.duration_ms / 1000:.3f} s, "
            f"{len(melody.notes)} notes"
        )

    write_wav(preview_dir / "00_all_melodies_v2_in_order.wav", combined)
    write_c_tables(root / "melody_tables_v2.generated.h", MELODIES_V2)
    print(f"Generated v2 previews in {preview_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
