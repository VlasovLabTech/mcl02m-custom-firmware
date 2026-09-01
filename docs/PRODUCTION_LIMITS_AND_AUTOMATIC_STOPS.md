# Production limits and automatic-stop audit

This document is the canonical inventory of custom-firmware limits that can change
real cooker behavior. It covers the production build, not the deliberately stricter
laboratory power-test firmware. Update it whenever a production limit, timeout,
debounce rule, or automatic Stop/Fault path changes.

Status: source version `0.2.33-dev`, 1 September 2026.

## Corrected IGBT behavior

- Native power-board `E07` is accepted only after two consecutive matching
  `R20=0x17` samples. At the normal 500 ms cadence, one isolated sample is ignored.
- During Start, cooking, or Pause, two consecutive valid IGBT readings above 92 °C
  activate a continuous advisory episode. The large `IGBT / >92°C` screen remains
  selected and three short beeps repeat every three seconds. Cooking continues with
  no power reduction. Each physical action is still executed and suppresses only the
  warning screen for seven seconds from the latest action; the beeps continue. The
  episode remains active at exactly 92 °C and clears only on a valid reading below
  92 °C or when the active session is stopped.
- Two consecutive valid in-session readings above 98 °C cause an interface-side E07
  and repeated Stop. Its OLED code has a small marker above the `7`; native
  power-board `R20=17` remains a plain E07. A single high sample is ignored.
- Start and fault acknowledgement are blocked while a valid IGBT temperature is
  above 80 °C. Start is permitted at exactly 80 °C after the ordinary healthy
  preflight succeeds.
- The laboratory power-test image still stops at 80 °C. That limit is appropriate
  only for short supervised bench tests and must never be enabled in production by
  accident.

## Automatic Fault or Stop paths

| Trigger | Confirmation/filter | Result | Origin/status |
|---|---:|---|---|
| Native known `R20` fault group | 2 identical consecutive samples | Latched stock E-code and repeated Stop | Stock-derived mapping; deliberate filter |
| IGBT raw sensor outside `0x41…0xF7` | 2 consecutive checksum-valid samples | `E08`, repeated Stop | Custom sensor-validity guard |
| Bottom NTC raw sensor outside `0x0B…0xFB` | 2 consecutive checksum-valid samples | `E08`, repeated Stop | Custom sensor-validity guard |
| Converted IGBT temperature `>98 °C` during a session | 2 consecutive valid samples | Marked interface `E07`, repeated Stop | Explicitly requested interface ceiling; native plain E07 remains separate |
| Converted bottom temperature `>210 °C` | 6 consecutive valid samples | `E05`-class interface Stop | Explicitly requested 210 °C emergency ceiling and filter |
| Critical I²C read path lost | 3 bad cycles enter 320 ms recovery; 5 s continuous loss faults | `E09`, repeated Stop | Current agreed recovery policy |
| Power-board command writes lost | 3 s continuous loss | `E09`, repeated Stop | Current agreed recovery policy |
| Cooking task stops renewing its lease | 3 s | `ECL`, lower task independently Stops | Custom safety watchdog; can terminate cooking if the cooking task stalls |
| Start acknowledgement missing | 8 s after first successful nonzero command heartbeat | `EST`, repeated Stop | Custom transactional safety timeout |
| Active-zero/Pause/Resume/pan-return acknowledgement missing | 3 s after command transmission | Latched transition fault, normally shown as `EPB` | Custom transactional safety timeout; field-review candidate |
| Temperature sensor communication becomes invalid while regulating | The temperature loop freezes immediately; critical I²C loss must remain continuous for 5 s to become `E09` | The last lower-board output command can remain in effect during the recovery interval | Custom continuity policy; important owner-review item |
| Retained cooking session reaches wall limit | 8 h including heating, active zero, Pause and NoPan | Transactional Stop, shown as `ETM` | Custom hard ceiling; review before sessions longer than 8 h |
| Manual Pause reaches 2 h | Continuous manual Pause only | Normal transactional Stop | Explicitly requested; POWER 0/profile wait is not Pause |
| Cooking timer or full profile reaches 5 h | Countdown/profile accounting | Normal completion Stop | Deliberate timer/profile cap |
| NoPan remains for the full melody | 3 consecutive NoPan samples, then one complete 128 s melody | `E02`, repeated Stop | Explicitly requested behavior |
| NoPan melody cannot start / cannot report completion | 30 s waiting to start / 132 s after reported start | `E02`, repeated Stop | Custom deadlock failsafe; can shorten or extend the nominal 128 s policy |
| Power board reports output active while interface is stopped | 4 consecutive samples | `STOP VERIFY` fault, repeated Stop | Custom fail-safe against unintended heating |
| Lower power task reports an unexpected Stop during an active session | Immediate classified observation | `ECL` if lease expired, otherwise `ETM` | Custom state-consistency guard |
| Delayed Start cannot establish a valid Start | 2 attempts; one retry after an immediate rejection or confirmed Start timeout | `EST` after the second failure | Custom scheduled-start safety policy; NoPan remains a separate path |
| Temperature-control output command fails | One failed application request | `EPB`-class fault | Custom state-consistency policy |
| Power-control task fails to service task watchdog | 5 s | ESP32 panic/reset; boot begins with Stop | ESP-IDF watchdog policy |
| Boot cannot read required power-board capabilities | Required `R25/R28/R29/R24/R2A/R2B` probe fails | Boot power-board fault; heating unavailable | Hardware-compatibility gate; important for another board revision |
| Start preflight is not exactly idle and healthy | One attempted Start; requires valid `R20/R22/R23/R24/R26`, `R20=00`, `R26=00`, both raw sensors in range, IGBT `<=80 °C`, and bottom `<=210 °C` | Start is rejected; manual Start warns, a due delayed Start retries once before `EST` | Custom conservative Start gate |
| Fault acknowledgement is not yet safe | Requires a verified Stop plus the same healthy preflight | Error remains latched and cannot be dismissed | Custom recovery gate |

`R21/R25/R27` runtime service-read failures do not independently cause `E09`.
Unknown nonzero `R20` values are warnings, not invented faults. `R20=2B/29/2A`
remain silent nonfault events.

The currently decoded native `R20` map is explicit: `0B→E03`, `0C→E04`,
`1B→E05`, `17→E07`, `15/16/18→E08`, `19/1A/1C/1D→E10`, and `01→E12`.
Every value in this known-fault group uses the same two-identical-sample filter.
`R20=00` is normal, and `R20=02` is NoPan rather than a generic fault.

An unknown nonzero `R20` is accepted as session-compatible during ordinary retained
operation, but it does **not** prove that cookware returned after NoPan and it does
not satisfy the exact `R20=00` cold-Start preflight. This distinction can prevent
Start or pan-return Resume without classifying the unknown value itself as a stock
fault.

## Command and timing constraints

| Constraint | Production value | User-visible effect |
|---|---:|---|
| Power range | `0…99` | POWER 0 is retained-session active zero |
| Active-zero command | `0D=81, 00=00, 0C=00` | Keeps the power-board session open instead of performing Stop; future board revisions still need supervised confirmation |
| Initial cold-Start output | target `1…10`: direct; `11…35`: 10; `36`: direct; `37…55`: 36; `56`: direct; `57…99`: 56 | The first nonzero command is already inside the target's final relay topology |
| Automatic ramp | at most 10 gear units per 500 ms heartbeat, only inside the selected target topology during cold Start | A high target still rises gradually without artificial relay/IGBT transitions through lower ranges |
| Automatic topology crossing after a live target change | direct `35↔56` when low and high ranges are crossed | The minimum necessary live relay transition remains; manual POWER 36…55 remains available |
| Small-cookware restriction | maximum gear 35 / topology A1 while `R26=01` | Applies in POWER, temperature, profiles and Resume; stock-derived behavior |
| Arm window | 30 s | Start must follow the internal Arm transaction before it expires |
| Stop confirmation | two fresh `R26=00` samples | UI does not claim completion until the lower board confirms Stop |
| Stop diagnostic timeout | 8 s | Stop continues indefinitely, but timeout evidence is recorded |
| Normal power-board heartbeat | 500 ms | Command/output changes are heartbeat paced |
| Recovery heartbeat | 320 ms | Used only after three critical-bad cycles |
| I²C bus speed | 10 kHz | Deliberately conservative shared-driver value; limits transaction throughput |

### Stock-ramp evidence and current difference

The stock `CookStepTask` decompilation writes its current command byte directly to
`W0C`; it contains no fixed `min(target, 10)` Start clamp and no universal `+10`
step. Passive captures independently include direct `STOP→gear 35`,
`STOP→gear 22/25`, and active `gear 35→99` commands in one 500-ms heartbeat.
Other traces show gradual sequences while the user was turning the encoder, but
those sequences are not evidence of a mandatory 10-level ramp. The exact
preselected `STOP→gear 99` experiment was not captured, so that one scenario must
not be described as directly observed.

Production `0.2.33-dev` deliberately retains a gradual custom cold Start but chooses
the entry gear from the final target topology: direct through 10, gear 10 for targets
11…35, gear 36 for targets 36…55, and gear 56 for targets 56…99. Exact boundary
targets 36 and 56 are therefore applied directly. Subsequent ten-level steps remain
inside that topology. This is a custom owner-approved policy rather than a claim
about the stock interface algorithm.

## Temperature-control constraints

| Constraint | Value/behavior | Origin/status |
|---|---|---|
| Selectable temperature | `40…190 °C` | Explicitly requested |
| Adaptive braking | 10…20 °C before target; at targets ≥170 °C at least 15 °C | Field-tuned custom policy |
| PREHEAT selection | gear 99 for error ≥30 °C, 77 for ≥18 °C, otherwise 56 | Custom speed/overshoot compromise |
| APPROACH selection | `min(35, 8 + 2×error)` until error ≤2 °C | Custom approach law |
| HOLD PI selection | `4 + 2×error + 0.08×integral`, rounded and clamped to `0…35` | Custom steady-state law |
| Phase hysteresis | PREHEAT is re-entered only beyond braking margin +5 °C | Avoids phase chatter but can delay renewed high power |
| Approach/Hold maximum | gear 35 | Deliberate avoidance of automatic 36…55 topology use |
| At/above setpoint | active zero | Explicitly requested retained-session behavior |
| Resume below setpoint | resumes when measurement is 1 °C below target | Current regulation behavior |
| Hold saturation notice | gear 35 for 90 s with error ≥3 °C | Warning only; does not Stop or raise the ceiling |
| Hot-surface indication | bottom temperature >60 °C | UI warning/sleep block only; does not limit cooking |
| IGBT advisory | 2 samples above 92 °C; stays active at 92 °C and clears below 92 °C | Three beeps every 3 s; screen may be hidden for 7 s by activity; no Stop or power reduction |

## UI and scheduling limits that do not stop active cooking

- Relative delayed Start is limited to 24 hours by the physical editor.
- There are five profiles with five stages each; their combined duration is limited
  to five hours.
- Automatic Sleep is configurable from 1 to 60 minutes and is blocked while the
  bottom surface is above 60 °C **only while both temperature readings are valid**.
  If either IGBT or bottom telemetry is invalid after cooking has stopped, the Hot
  indication and Sleep interlock do not use the stale bottom value. It is not entered
  from active cooking states.
- OLED timeout choices range from one minute to five hours. Display timeout does not
  Stop cooking.
- The completion picture remains for at most one minute before Hot/OLED/Sleep policy
  resumes.
- The physical UI accepts Sleep timeout values only from 1 to 60 minutes and OLED
  timeout values from the fixed list 1 minute…5 hours.
- The local web UI deliberately cannot Start, Stop, Pause, Resume, schedule heating,
  or change a live cooking setpoint. Those operations require physical controls.
- Mi Home, MIoT/Xiaomi cloud, NFC and stock recipes are not implemented.
- `SOUND OFF` does not silence NoPan, critical alarms, or the IGBT advisory.
- An unknown-`R20` warning consumes the first physical input as acknowledgement.
  The IGBT advisory instead executes every input and only hides its screen for seven
  seconds after the latest action; the periodic sound is not acknowledged.

## Items that deserve explicit owner review

The following are not stock cooker limits and can surprise a user even though they
were added as safety/state-consistency mechanisms:

1. Two-sample raw IGBT and bottom-NTC validity faults.
2. Interface E07 after two IGBT samples above 98 °C and Start/ACK inhibition above
   80 °C.
3. Six-sample bottom emergency cutoff strictly above 210 °C.
4. Three-second cooking lease.
5. Three-second non-Start transition acknowledgement deadline.
6. Eight-hour retained-session wall limit.
7. Target-topology cold Start plus the 10-level-per-heartbeat in-topology ramp; this
   remains a custom policy rather than a universal stock-interface rule.
8. Required boot readability for `R25/R28/R29/R24/R2A/R2B`.
9. Exact cold-Start preflight and healthy-preflight requirement for clearing faults.
10. Unknown `R20` values cannot prove cold-Start or pan-return readiness.
11. The 30 s / 132 s NoPan audio failsafes can differ from the nominal melody time.
12. A temporary sensor-communication gap freezes temperature regulation at the last
    lower-board command while I²C recovery is attempted.
13. Hot/Sleep protection requires both sensor readings to be currently valid, so a
    post-Stop telemetry loss can permit Sleep without using the last hot reading.
14. Delayed Start retries once, but two failed attempts still become `EST`.

Do not change these silently. Any adjustment must update this document, policy tests,
the user manual where user-visible, and the build manifest.
