# Changelog

## 0.2.4-dev — 2026-08-24

- Added complete English, Russian, and Simplified Chinese local UI.
- Added POWER and regulated TEMPERATURE modes.
- Added five presets with five timed sequential stages each.
- Added timer with seconds, manual/SNTP clock, START IN and START AT.
- Added local Wi-Fi provisioning, authenticated settings/profile web UI, and a
  physical custom Factory reset; remote heat control remains prohibited.
- Added ten OLED state images and six approved PWM melodies.
- Added NoPan recovery, critical alert envelope, PI saturation warning, thermal
  guards, hard run limit, watchdog, and safe boot Stop.
- Documented the panel hardware and the 0x2A power-board I²C protocol.
- Added a self-contained trilingual HTML user manual.

This remains a development release, but the current NTC readings and
temperature-controller behavior were accepted by the owner after supervised
real-use tests; no separate calibration step is currently planned.
