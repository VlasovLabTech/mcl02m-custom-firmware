# Sound Assets

- `final/` contains the six approved PWM melodies, C note tables, and WAV
  previews used by the production firmware.
- `experiments/` preserves earlier design generations and their preview tools.

The production copy consumed by the build is
`firmware/production/main/melody_tables.h`.

Approved events: boot, wake, complete, NoPan, critical error, and Sleep. Normal
duty is 50%; the Sleep melody is approximately 18%. NoPan and critical alerts
remain mandatory when normal sounds are disabled.
