# Changelog

## 0.2.34-dev — 2026-09-01

- Moved the mandatory active-session IGBT advisory to the buzzer's louder
  4 kHz region. Each three-second warning cycle now contains three 300 ms tones
  with 100 ms gaps at the unchanged 50% PWM duty.
- Expanded the self-contained EN/RU/ZH user manual with one temperature-threshold
  table and explicit small-cookware behavior across POWER, T°C, and preset stages.

## 0.2.33-dev — 2026-09-01

- Changed only the cold-Start ramp entry point so the first nonzero command is
  already inside the requested target's final relay topology: targets `1…10` start
  directly, `11…35` start at 10, `36` starts directly, `37…55` start at 36,
  `56` starts directly, and `57…99` start at 56.
- The existing ten-level/500-ms ramp continues only inside that selected topology.
  It can no longer force one or two unnecessary relay/IGBT transitions merely while
  climbing from 10 to a middle- or high-range cold-Start target. Active-zero Resume,
  Pause Resume and pan-return Resume retain their separately confirmed direct-output
  behavior.

## 0.2.32-dev — 2026-09-01

- Changed the active-session IGBT advisory into a continuous condition. After two
  valid readings above 92 °C, the OLED keeps selecting `IGBT / >92°C` and three
  short beeps repeat every three seconds. Physical input still works and suppresses
  only the warning screen for seven seconds after the latest action. The episode
  stays active at exactly 92 °C, clears below 92 °C, and is cancelled by Stop.
- Added an interface-side IGBT ceiling: two consecutive valid session readings above
  98 °C produce a repeated-Stop E07 whose OLED code carries a small marker above the
  `7`. Native power-board `R20=17` remains a plain E07. Start and fault
  acknowledgement are blocked above 80 °C.
- Debounced raw IGBT/bottom sensor faults to two consecutive valid samples and the
  separate bottom-temperature cutoff to six consecutive samples strictly above
  210 °C.
- Delayed Start now makes at most two attempts. One retry covers both an immediate
  Start rejection and an actual lower-board Start-confirmation timeout; NoPan stays
  on its separate cookware path.
- Audited the stock power ramp from the original dump and passive captures. The
  stock interface has no universal gear-10 Start clamp or fixed `+10` ramp; direct
  `STOP→35`, `STOP→22/25`, and active `35→99` commands were captured. The existing
  custom ramp remains unchanged pending a separate owner decision.

## 0.2.31-dev — 2026-09-01

- Removed the accidental production IGBT cutoff at 80 °C. That conservative limit
  came from the supervised laboratory power-test image and is now explicitly
  compiled out of both public and private production builds.
- Kept the power board's native E07 protection: `R20=17` must still be observed in
  two consecutive matching 500-ms samples before the stock IGBT-overheat fault is
  latched.
- Added an advisory-only IGBT temperature warning. Two consecutive valid readings
  above 92 °C play a mandatory two-beep warning and show a large
  `IGBT / >92°C` screen without stopping or reducing cooking power. Any physical
  action dismisses the screen without being swallowed; the warning rearms only
  after the IGBT reading falls to 88 °C or below.
- Corrected an invalid interface-side IGBT raw reading to map to the sensor-fault
  class E08 instead of being mislabeled as a temperature E07.
- Added a canonical production-limit and automatic-stop audit documenting every
  custom timeout, cutoff, watchdog, ramp, debounce and user-visible functional cap.

## 0.2.30-dev — 2026-08-31

- Replaced the coarse six-cycle E09 rule with classified, time-based communication
  recovery. Runtime `R20/R22/R23/R24/R26` reads and all `0D/00/0C` writes are
  critical; service-only `R21/R25/R27` failures remain diagnostic and cannot cause
  E09 by themselves.
- After three consecutive critical-bad cycles, the power task temporarily polls only
  the critical set on a 320-ms heartbeat. Two complete good critical cycles return it
  to the normal 500-ms/full-register schedule.
- E09 now requires either five seconds of continuous critical-path loss or three
  seconds of continuous control-command write loss. A latched fault still repeats
  the complete Stop sequence.
- Added immutable first-cause RAM evidence for E09: read/write failure masks, valid
  mask, `R20/R26`, last `0D/00/0C`, elapsed loss timers, recovery count, state and
  cycle counters are exposed in compact UART and authenticated status diagnostics.
- Extended executable and static gates for transient recovery, stable two-cycle exit,
  service-only failures, critical-read timeout, command-write timeout, Stop recovery,
  and incident immutability.

## 0.2.29-dev — 2026-08-30

- Completed the production OLED language audit for English, Russian, and
  Simplified Chinese. The firmware/version and power-board headings are now
  translated in Chinese, the unknown-`R20` warning is localized in all three
  languages, and the Chinese `STOPPING` state no longer says Pause.
- Added the required compact Chinese glyphs and strengthened the localization gate
  to check Chinese fallback labels, the localized warning, Russian compact width,
  and complete CJK coverage.
- Updated the self-contained trilingual user manual for the current temperature,
  active-zero, small-cookware, Hot/Ready, delayed-start, NoPan, private-sound,
  session-limit, warning, and fault-code behavior. The manual now states explicitly
  that LANGUAGE controls the OLED while the local web page remains English-only.
- Bounded the timer-completion Ready state to one minute. If it is not acknowledged,
  the engine returns to Idle so Hot, OLED timeout, and Sleep behavior resume normally.
- Corrected NoPan input handling: short center, long center, and Cancel now converge
  on the same idempotent Stop. NoPan can no longer enter a Pause transaction whose
  unconfirmed active-zero response could surface as the generic `EPB` fault.
- Recorded the explicitly authorized `0.2.28-dev-private` app-only deployment to
  stock `ota_1`; bootloader, partition table, `otadata`, NVS and every other
  partition remained untouched.

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
