# Supervised Hardware Validation Plan

Status: `0.2.28-dev-private` was flashed app-only after explicit authorization;
supervised sound/localization confirmation remains pending. The targeted `0.2.24-dev` boot, Delayed Start and
transactional Stop validation passed. This document does not authorize another flash
or heating session by itself.

The objective is to validate the current fix and regression-sensitive
state-machine paths with one passive UART capture. The operator performs all physical
actions; the monitor only reads the serial port. No remote heating commands are added.

## 1. Evidence available in the validation build

UART diagnostics use compact comma-separated frames, not periodic JSON:

- `B,required_mask,service_mask,service_failures,R25,R28,R29,R24,R2A...R2F`
  is emitted once after the startup probe. The complete required mask is `0F30`.
  `R2C`–`R2F` are best-effort service diagnostics: their failures are recorded in
  `service_mask/service_failures` but do not fail boot. A missing required
  `R24/R25/R28/R29/R2A/R2B` read still prevents operation.
- `C,event,state,mode,selected,applied,bottom,active_zero,timer_s,transition_gen,`
  `transition_kind,pending,i2c_bad,detail` records every cooking-layer decision.
- `D,...` is the complete 500-ms lower-layer snapshot. The validation monitor
  decodes state, target/applied output, command, `R20/R26/R28`, temperatures, I2C
  count, cookware limit, lease, transition and fault fields.
- `T/S/L/P/Z/N/F/I/W/X` retain the existing transition, Stop, lease, feedback,
  active-zero, pan-return, fault, I2C, warning and Start-incident evidence.
- `B/D/U/E/T/C` from the input layer retain button, encoder, touch and cooking
  decisions. Prefixes are disambiguated by their ESP log tag in the raw capture.

The passive monitor is:

```powershell
python tools\monitor_uart.py --list
python tools\monitor_uart.py COM_PORT --minutes 15
```

It opens DTR/RTS deasserted, never writes bytes, saves every UART line under the
ignored `_local_private/validation/` directory, and prints only events plus changed
`D` summaries. Use `--verbose` only when every periodic line is needed on screen.
Only one program may own the serial port; do not run `idf.py monitor` at the same
time.

## 2. Before the supervised run

1. Run all offline gates and record the exact app image version, size and SHA-256.
2. Obtain explicit authorization for that exact app-only image. Do not use the broad
   `idf.py flash` command and do not write bootloader, partition table, OTA data,
   NVS, PHY or assets.
3. After the authorized app-only write and boot selection, start the passive monitor
   before normal cooker startup. No backup or unrelated flash operation is part of
   this run.
4. Keep suitable cookware and water available. Keep the cooker attended throughout
   every powered test.

Stop the run immediately if output does not go to zero after Stop, a fault repeats,
temperatures are implausible, an unexpected relay sequence occurs, the monitor loses
the serial stream during an uncertain transition, or the operator considers the
physical behavior unsafe. Disconnect mains if the normal Stop path cannot be trusted.

## 3. Short critical session (target: 10–15 minutes)

Perform the steps in order. Do not continue after a failed prerequisite.

| Step | Operator action | Required evidence and pass condition |
| --- | --- | --- |
| 1. Boot/idle | Power the cooker and leave it idle for 30 seconds. Open the firmware/board version screen. | One `B` frame; required mask `0F30`; `R25/R28/R29` and `R2A–R2F` preserved. Service failures may be nonzero without `BOOT I2C`. `D` settles in `STOPPED`, `R26=00`, no fault. |
| 2. Low POWER Start | Use water cookware, select POWER 10, Start, wait for stable feedback. | `C,start`, `T,BEGIN ... START`, a transmitted nonzero command, then `T,DONE`; cooking becomes `COOKING`. `R26=01` or `02` confirms Start; no early success before command transmission. |
| 3. Active zero | Turn POWER to 0, wait three seconds, then select POWER 20. | Zero sends `81/00/00`, confirms `ACTIVE_ZERO`, and does not send a full Stop. Resume is one confirmed generation. Record relay/fan observations. |
| 4. Pause/Resume | Short-center Pause, wait three seconds, short-center Resume. Repeat one short press while a transition is pending. | Input `B` frame exists for every press. One `C,manual_pause_requested`, one confirmed Pause, one Resume generation. A repeated press is ignored/idempotent rather than inverted. No full Stop command during retained Pause/Resume. |
| 5. User Stop | Long-center Stop and wait for completion. | `S,BEGIN`, repeated Stop heartbeats as needed, two valid zero-feedback samples, `S,DONE`; only then cooking enters `IDLE`. |
| 6. Timer editing | Set seconds, confirm; set minutes, confirm; set hours, confirm. Disable it, then set a short timer again. | Each field is independently editable; set → disable → set is immediately visible. Timer expiry enters `STOPPING`, and completion is reported only after `S,DONE`. |
| 7. Delayed Start UI | Schedule a short relative Start, visit a different menu, and wait for expiry. Then repeat once and cancel near (but before) its deadline. | The waiting screen orders mode, selected value, delay label and countdown. Expiry selects the actual live POWER/TEMPERATURE/profile view, and pending Start feedback cannot produce stale-snapshot `ETM`. Cancel remains stopped and cannot be revived by the elapsed deadline. |

If the 15-minute serial window ends, stop safely and start a new passive capture
before the next group. Logs from separate windows may be correlated by the firmware
hash and the one-shot `B` tuple.

## 4. Recovery and cookware session

1. Start at low POWER with suitable cookware.
2. Remove the cookware well before the 128-second NoPan melody ends. Confirm
   `NO_PAN`, zero applied output, frozen countdown and uninterrupted playback.
3. Return the same cookware. Require a confirmed `PAN_RETURN_HOLD` active-zero
   generation before the separately recomputed `PAN_RETURN_RESUME` generation.
4. Repeat with the known small cookware. `R26=01` must cap the honest selected and
   applied output at 35, keep topology `A1`, and show the small-cookware notice.
   A request above 35 must be rejected with the notice again; a lower request remains
   allowed.
5. Exercise Stop once during NoPan and once during pan-return pending. Late feedback
   from the cancelled generation must never restore heat.

## 5. Temperature session

1. Start with water at 58 °C. Record PREHEAT, APPROACH, HOLD, maximum temperature,
   applied gear and time to settle.
2. While the measured temperature is above a newly lowered target, select that lower
   target. The retained session must enter active zero immediately, remain running,
   and resume ordinary regulation only after cooling below the setpoint.
3. Pause and Resume in temperature mode. The first resumed output must be recomputed
   from the current reading; old PI state must not be reused.
4. If a natural I2C reading gap occurs, require one
   `C,temperature_reading_gap`. The live PI phase is retained, but old four-second
   trend evidence is discarded; fresh samples must rebuild the trend window.
5. After the low-temperature test passes, repeat at 125 °C and record first-heat
   overshoot separately from steady HOLD behavior. Do not change controller tuning
   during the same evidence run.
6. A 190 °C test is a later, separately supervised step. It is not required to prove
   the state transitions above and must not be the first thermal test.

Do not inject I2C loss, sensor faults or overtemperature while heating. Naturally
observed failures are captured; deliberate fault injection belongs to a separate
bench procedure.

## 6. Profiles and boundary cases

Use a short test profile containing a nonzero POWER cell, a POWER-0 wait cell and a
final nonzero or temperature cell.

- Zero-duration cells must be skipped.
- A POWER-0 cell is ordinary timed active zero, not manual Pause, and has no two-hour
  Pause timeout.
- If a cell countdown reaches zero while an output transition is still pending, the
  next cell must wait. It may advance only after that transaction is confirmed; it
  must not produce `PROFILE STAGE FAILED` from transition contention.
- Pause/Resume at each cell type freezes the profile countdown and preserves the
  current cell. Timer/profile completion must use transactional Stop.
- Long-center Stop must win even while the timer editor or another live overlay is
  open.

## 7. Second-cooker compatibility gate

On another cooker, capture the boot `B` frame before any heating. Compare
`R25/R28/R29` and the masks with the validated cooker. A different `R28` is not
evidence that the current NTC lookup table is compatible: stop before heating and
add the matching stock conversion evidence first. `R29` is recorded for later
cross-channel analysis; no meaning is invented from one sample.

## 8. Result record

Append one row per supervised session:

| Date/time | Firmware version | App SHA-256 | Cooker | Boot `B` tuple | Steps | Result | Log path | Physical observations |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-08-30 01:51–02:16 MSK | `0.2.24-dev` | `f547b7dd4ba9bddb1a50b5ff6a9ad9d920a579b77a55acac8e06d3c8571fd21c` | Development unit | required `0F30`; service `F000/0`; `R25/R28/R29=0A/0B/32` | Boot, version, delayed POWER 10 Start, delayed cancellation, delayed TEMPERATURE 55 °C Start, POWER 10 NoPan/return, user Stops | PASS | `_local_private/validation/uart-20260830-015103.log`; `_local_private/validation/uart-20260830-020518.log`; `_local_private/validation/uart-20260830-021359.log` | Both delayed modes reached confirmed `COOKING` without `ETM`; cancellation did not revive Start; temperature reached 55 °C and entered active zero; NoPan forced zero, return confirmed `PAN_RETURN_HOLD` before a separate `PAN_RETURN_RESUME` to gear 10; all Stops reached confirmed `R26=00`, `S,DONE`, `IDLE` |

Pending for the next supervised `0.2.26-dev` session: repeat step 7 and verify the
new persistent `time` artwork, lower-right countdown/mode badge, expiry into the
selected cooking mode, and cancellation before the deadline.

Pending on the deployed `0.2.28-dev-private`: verify one full 128-second NoPan melody,
E02 only after its final note, cookware return before completion without E02, and a
second removal restarting from the first note. For the private flavor, verify that
ordinary input does not interrupt or queue delayed clicks behind Wake, Cancel stops
Wake, long-hold Sleep replaces Wake, and Sleep finishes before a queued Wake begins.

Keep these decisions open until hardware evidence exists:

- `R28`-selected NTC lookup families and unsupported-revision blocking;
- exact per-code immediate/filtered stock `R20` behavior;
- interpretation of `R29` cross-channel behavior;
- retained-session direct Resume versus a different ramp policy;
- thermal braking and saturation tuning for different cookware/load combinations.
