# Changelog

## 0.2.6-dev — 2026-08-28

- Fixed a false `STOP VERIFY` / `EPB` fault after entering manual Pause or another
  active-zero state. Retained sessions are no longer checked as fully stopped.
- Preserved the first latched power-board fault so a later safety observation cannot
  hide the original cause.
- Replaced repetitive UART JSON with a fixed compact diagnostic frame containing
  the complete power-board state, commands, `R20…R27`, temperatures, counters and
  fault reason. Short transition/error frames remain enabled for live diagnosis.
- Kept the full human-readable JSON snapshot available through authenticated Wi-Fi.

## 0.2.5-dev — 2026-08-28

- Added the experimental stock-derived active-zero command (`0x81/0x00/0x00`)
  for POWER gear 0, temperature-control coast, and manual Pause.
- Resume from active zero no longer deliberately sends a full Stop/re-arm cycle.
- Added a full Stop after two continuous hours of manual Pause. POWER gear 0 and
  POWER-0 profile stages remain ordinary active sessions and do not use that
  Pause timeout.
- Allowed timed POWER-0 profile stages as non-heating wait stages.
- Added compile-time removable active-zero UART/Wi-Fi diagnostics, including the
  last command frame and transition counters.
- Enlarged every live OLED fault code to 2× and removed the misleading separator
  stroke left behind by the example code in the source artwork.
- Kept faults and runtime control out of NVS; no automatic NVS erase was added.

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
- Expanded the selectable temperature range from 40–175 °C to 40–190 °C and
  removed the lab-only 120 °C bottom-sensor guard from production; the stock
  power MCU's native E05 protection remains active.

This remains a development release. Supervised cooking exposed and removed a
lab-only production guard; high-power I²C reliability still needs diagnosis.
