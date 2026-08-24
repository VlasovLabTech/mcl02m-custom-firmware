#!/usr/bin/env python3
"""Generate event-specific public-domain chiptune melodies for MCL02M."""

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


# Equal-tempered notes rounded to integer hertz.  Every sound is a monophonic
# PWM note sequence.  Nothing in this pack requires PCM playback or polyphony.
G5 = 784
A6, B6 = 1760, 1976
C7, D7, DS7, E7, F7, FS7, G7, A7, AS7, B7 = (
    2093, 2349, 2489, 2637, 2794, 2960, 3136, 3520, 3729, 3951,
)
C8, D8, DS8, E8 = 4186, 4699, 4978, 5274


MELODIES_V3 = (
    Melody(
        c_name="boot_v3_korobeiniki",
        wav_name="01_power_on_v3_korobeiniki.wav",
        title="Power on v3 / Korobeiniki new-game boot",
        purpose="recognizable Game Boy-era energy with a new power-up cadence",
        notes=(
            # Korobeiniki opening phrase, newly timed for this buzzer.
            Note(E7, 240, 20), Note(B6, 120, 20), Note(C7, 120, 20), Note(D7, 240, 20),
            Note(C7, 120, 20), Note(B6, 120, 20), Note(A6, 240, 20), Note(A6, 120, 20),
            Note(C7, 120, 20), Note(E7, 240, 20), Note(D7, 120, 20), Note(C7, 120, 20),
            Note(B6, 300, 40), Note(C7, 120, 20), Note(D7, 240, 20), Note(E7, 240, 20),
            Note(C7, 240, 20), Note(A6, 240, 40), Note(A6, 360, 120),
            # Original arcade start cadence: READY -> GO.
            Note(E7, 80, 20), Note(G7, 80, 20), Note(B7, 80, 20), Note(E8, 320),
        ),
    ),
    Melody(
        c_name="complete_v3_ode_to_joy",
        wav_name="02_cooking_complete_v3_ode_to_joy.wav",
        title="Cooking complete v3 / Ode to Joy victory",
        purpose="a universally recognizable success phrase with arcade resolution",
        notes=(
            # Beethoven: Ode to Joy, first phrase.
            Note(E7, 140, 25), Note(E7, 140, 25), Note(F7, 140, 25), Note(G7, 140, 25),
            Note(G7, 140, 25), Note(F7, 140, 25), Note(E7, 140, 25), Note(D7, 140, 25),
            Note(C7, 140, 25), Note(C7, 140, 25), Note(D7, 140, 25), Note(E7, 140, 25),
            Note(E7, 220, 25), Note(D7, 120, 25), Note(D7, 300, 100),
            # Original level-clear sparkle.
            Note(C7, 80, 20), Note(E7, 80, 20), Note(G7, 80, 20), Note(C8, 360),
        ),
    ),
    Melody(
        c_name="no_pan_v3_fur_elise",
        wav_name="03_no_pan_v3_fur_elise.wav",
        title="No pan v3 / Fur Elise attention",
        purpose="polite, instantly recognizable attention request rather than a fault",
        notes=(
            # Beethoven: Fur Elise opening question.
            Note(E8, 150, 35), Note(DS8, 150, 35),
            Note(E8, 150, 35), Note(DS8, 150, 35),
            Note(E8, 220, 50), Note(B7, 220, 50),
            Note(D8, 220, 35), Note(C8, 220, 35), Note(A7, 600),
        ),
    ),
    Melody(
        c_name="critical_motif_v3_beethoven_fifth",
        wav_name="04_critical_error_v3_beethoven_fifth.wav",
        title="Critical error v3 / Beethoven Fifth alarm",
        purpose="the classic short-short-short-long fate rhythm in the loud range",
        notes=(
            # Beethoven's Fifth rhythm, transposed into the buzzer's loud region.
            Note(B7, 140, 40), Note(B7, 140, 40), Note(B7, 140, 40), Note(G7, 520, 120),
            Note(A7, 140, 40), Note(A7, 140, 40), Note(A7, 140, 40), Note(FS7, 520, 120),
            Note(4000, 140),
        ),
    ),
    Melody(
        c_name="sleep_v3_brahms",
        wav_name="05_sleep_v3_brahms_lullaby.wav",
        title="Sleep v3 / Brahms lullaby",
        purpose="recognizable lullaby phrase followed by a slowing power-down fall",
        notes=(
            # Brahms: Wiegenlied opening phrase, arranged as a simple pulse melody.
            Note(G7, 180, 40), Note(G7, 180, 40), Note(B7, 360, 120),
            Note(G7, 180, 40), Note(G7, 180, 40), Note(B7, 360, 120),
            Note(G7, 180, 40), Note(B7, 180, 40), Note(E8, 360, 40),
            Note(D8, 180, 40), Note(C8, 300, 40), Note(C8, 180, 40), Note(B7, 360, 160),
            # Original power-down tail, progressively lower and slower.
            Note(G7, 160, 40), Note(E7, 200, 40), Note(C7, 500),
        ),
    ),
)


def main() -> int:
    root = Path(__file__).resolve().parent
    preview_dir = root / "previews_v3_classics"
    combined: list[int] = []
    separator = [0] * milliseconds_to_samples(1500)

    for index, melody in enumerate(MELODIES_V3):
        pcm = melody_pcm(melody)
        write_wav(preview_dir / melody.wav_name, pcm)
        if index:
            combined.extend(separator)
        combined.extend(pcm)
        print(
            f"{melody.wav_name}: {melody.duration_ms / 1000:.3f} s, "
            f"{len(melody.notes)} notes"
        )

    write_wav(preview_dir / "00_all_v3_classics_in_order.wav", combined)
    write_c_tables(root / "melody_tables_v3_classics.generated.h", MELODIES_V3)
    print(f"Generated v3 previews in {preview_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
