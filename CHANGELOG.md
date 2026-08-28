# Changelog

## 0.2.9-dev — 2026-08-28

- Removed the three-degree temperature restart hysteresis after the 58 °C test
  demonstrated the intended `58 → 55 °C` coast but the resulting regulation was too
  loose. Temperature mode now remains in active zero at or above the setpoint and
  lets the normal PI controller resume at the first whole degree below it.
- Retained zero-power Start above the setpoint, the stronger `0.2.8-dev` controller
  tuning and the gear-35 APPROACH/HOLD cap.

## 0.2.8-dev — 2026-08-28

- Completed every serial LED-driver update with the final STB latch edge and added
  a compact change-only `L` diagnostic frame for the white, orange, blue and timer
  outputs. This independent protocol hardening is not presented as the cause of the
  LED outage: the owner confirmed that the shared-channel hardware problem predates
  the current firmware changes.
- Kept the I2C bad-cycle counter's internal clean-cycle reset immediate while holding
  the highest displayed digit for at least two seconds.
- Made the temperature editor own and render its clamped setpoint immediately, so
  entering `T°C` cannot briefly reuse an asynchronous stale display value.
- Restored the previously tested stronger PREHEAT, APPROACH and PI tuning while
  preserving active-zero coast, the three-degree restart hysteresis and gear-35 cap.

## 0.2.7-dev — 2026-08-28

- Fixed manual Resume from active-zero Pause. Resume now validates a healthy retained
  power-board session (`R20=0`, valid sensors and nonzero `R26`) instead of incorrectly
  requiring the fully stopped `R26=0` state.
- Added compact UART frames for main-button, encoder and touch input events, plus
  Pause/Resume requests, results and explicit rejection diagnostics.
- Counted manual Pause entries in the active-zero diagnostic counters.

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
