# Changelog

## 0.2.16-dev — 2026-08-29

- Replaced the queued timer toggle with synchronous, explicit Set and Disable
  operations. Rapid disable/reopen/set sequences can no longer observe stale state or
  invert a later command by processing two delayed toggles.
- Split the cooking-timer editor into three confirmations: seconds, minutes, then
  hours. Seconds and minutes share the `MM:SS` screen with only the active field
  blinking; hours retain their separate screen.
- Added bounded fast rotation for seconds/minutes, rejected an all-zero timer instead
  of silently closing without a visible countdown, and retained the five-hour limit.
- Added executable lifecycle and field-composition tests plus static regression gates
  for synchronous timer mutation and the three-stage editor.

## 0.2.15-dev — 2026-08-29

- Removed the temporary I2C-loss counter from the production OLED and physical
  Settings menu without deleting the implementation. A single disabled compile-time
  flag retains the overlay, two-second peak hold, stored setting, and menu path for a
  future diagnostic build.
- Kept the internal consecutive-I2C-error counter, E09 threshold, compact UART
  diagnostics, and authenticated status diagnostics unchanged.
- Recorded the successful `0.2.14-dev` development-unit deployment and retained-
  session active-zero test without unexpected relay switching.
- Reassessed the deferred state-machine packages against the newly decoded
  `R20/R26/R28` behavior: the obsolete `R20=2B` timeout work is closed, Start/EST now
  focuses on immutable evidence and deterministic tests, and a separate power-board
  revision compatibility package precedes work with a second cooker.

## 0.2.14-dev — 2026-08-29

- Extended `SETUP → FIRMWARE → VERSION` with the live raw `R28` power-board
  revision/type byte. All four firmware/board rows are now left-aligned.
- Made `R20=2B` a silent, unbounded, nonfault relay-transition status. It remains
  visible in raw diagnostics and no longer escalates after ten seconds.
- Made stock service events `R20=29/2A` silent nonfaults during normal cooking; their
  raw values remain available in diagnostics.
- Replaced generic `EPB` escalation for every other unknown nonzero `R20` with a
  persistent warning that displays the exact hexadecimal value. The first physical
  input acknowledges only the warning, without changing power or Pause state.
- Kept the known `R20` fault groups as faults and made their debounce require
  consecutive samples of the same value. Unknown warnings no longer obstruct an
  already-started session's Start confirmation or retained-session Resume.
- Added `R28` and unknown-warning fields to compact UART and authenticated Wi-Fi
  diagnostics, plus policy/static regression coverage.

## 0.2.13-dev — 2026-08-29

- Reclassified `R26=01` from an unrecognized response to the stock firmware's valid
  restricted-cookware acknowledgement.
- Enforced the stock gear-35/`A1` limit below POWER, TEMPERATURE, profile, ramp and
  retained-session Resume paths, so no control mode can bypass the capability.
- Made POWER display the real permitted value. A three-second small-cookware notice
  precedes the normal capped display; attempts to raise the value above 35 are
  rejected with the notice, while downward adjustments remain available.
- Added compact UART and authenticated-status visibility for the cookware limiter,
  executable policy tests, and static safety contracts.
- Re-audited the stock ESP32 dump across `R20…R2F`, documenting known status groups,
  service-only values, capability bytes, board-revision behavior, and remaining
  compatibility work without claiming knowledge of the separate power MCU firmware.

## 0.2.12-dev — 2026-08-28

- Eliminated the Set-to-Start ordering race: mode, POWER and temperature edits now
  complete under the cooking-state lock, while Start joins the ordered intent queue.
- Protected an active delayed Start from mode, profile, POWER and temperature
  mutation. Rejected physical mode selections remain on the current screen and play
  the warning sound.
- Added an explicit Stop rollback when Arm succeeds but the following Start request
  fails.
- Reset the elapsed NoPan window when manual Pause is entered from NoPan, so a later
  Resume receives a fresh 60-second cookware-return window.
- Applied temperature target changes immediately during STARTING/COOKING. Missing
  sensor data selects retained-session active zero; a failed safe-output update
  latches a fault and requests Stop.
- Added executable policy cases and static contract gates for all five transitions.

## 0.2.11-dev — 2026-08-28

- Added `SETTINGS → FIRMWARE → VERSION`. The screen displays the compile-time
  firmware version as two small right-aligned lines centered vertically on the OLED.
- Preserved the complete state-machine audit as a deferred, test-driven
  implementation plan. This release does not claim to implement those control-flow
  recommendations.

## 0.2.10-dev — 2026-08-28

- Replaced the fixed ten-degree PREHEAT/APPROACH boundary with a bounded adaptive
  braking margin. The controller adds the positive temperature rise observed over
  four seconds to a 10 °C base, caps the result at 20 °C, and enforces at least
  15 °C of braking reserve for targets from 170 °C upward.
- Latched a five-degree phase hysteresis at APPROACH entry so the falling heat rate
  cannot make PREHEAT and APPROACH chatter.
- Made temperature Pause continue observing the NTC trend without integrating PI.
  Resume now clears PI timing, calculates the current output, and updates the paused
  power-board target before the retained session resumes, preventing an old-power
  pulse after Pause.
- Made the gear ramp cross directly between the low `1…35` and high `56…99`
  topologies when the requested target lies outside the middle range. Automatic
  temperature control no longer commands transient gears `36…55`; explicitly
  selected manual POWER gears in that range remain supported.

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
