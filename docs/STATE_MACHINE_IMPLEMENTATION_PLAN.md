# State-Machine Audit and Deferred Implementation Plan

Status: **deferred; no control-logic fixes from this document have been implemented yet**  
Audit baseline: `0.2.10-dev`, commit `2b5784e` (`2026-08-28`)  
Current source after the unrelated firmware-version UI addition: `0.2.11-dev`

This document preserves the complete control-flow review so the findings can be
discussed, prioritized, and implemented later without relying on chat history. It is
an engineering plan, not a claim that every listed behavior is already a confirmed
field failure. Each item distinguishes a demonstrated code path from a risk that
still requires a targeted test.

The audit covered:

- the cooking state machine and its intent queue;
- the power-board state machine and the 500 ms I2C control cycle;
- Start, Stop, Pause, Resume, active zero, NoPan and fault transitions;
- POWER, TEMPERATURE and profile behavior;
- timers, delayed Start, Sleep/Wake and menu interactions;
- temperature-controller phase transitions and topology changes;
- boot, watchdog, communication-loss and hard-runtime behavior;
- the user actions that can occur while any transition is in progress.

No flashing is authorized by this plan. Every implementation phase must be built and
checked offline first, then flashed only after an explicit instruction.

## 1. Executive conclusion

The normal steady-state paths are coherent: POWER and TEMPERATURE can start, active
zero retains the power-board session, Pause has a separate two-hour timeout, the
temperature controller observes temperature while paused, and the power task keeps
its own I2C heartbeat and safety watchdog.

The main architectural weakness is that the application currently treats several
requested transitions as completed before the power board has physically confirmed
them. The most important example is Stop: the cooking layer immediately becomes
`IDLE` or `COMPLETE`, while the power task only begins sending the Stop sequence. A
second weakness is that only the power task has a watchdog. If the higher-level
cooking task stops running while the power task remains alive, the power task can
continue sending the last valid heating command.

Before calling the firmware robust, the recommended minimum is:

1. add a confirmed `STOPPING` transition;
2. add a cooking-command lease consumed by the power task;
3. serialize configuration and Start operations;
4. make NoPan/Pause/Resume transitions explicitly reset and recompute their context;
5. reconcile the five-hour power-board deadline with active-zero and profile waits;
6. add transition-matrix tests and fault-injection tests.

## 2. Existing state model

### 2.1 Cooking layer

| State | Meaning | Main exits |
| --- | --- | --- |
| `SLEEP` | UI sleep; no active run | Wake, select a mode, schedule Start |
| `IDLE` | stopped and ready for a new selection | Select mode/profile, Sleep, schedule |
| `READY` | mode/profile has been configured | Start, reconfigure, Sleep, schedule |
| `DELAYED` | waiting for relative or absolute Start | Due time, Cancel/Stop |
| `STARTING` | Start requested; waiting for heat confirmation | Cooking, Pause, Stop, NoPan, Fault |
| `COOKING` | active session, including gear-zero coast | Pause, Stop, NoPan, Complete, Fault |
| `PAUSED` | manual retained-session active zero | Resume, Stop, two-hour timeout, Fault |
| `NO_PAN` | power board reports missing cookware | Pan return, Pause, Stop, timeout, Fault |
| `COMPLETE` | timer/profile completed | Acknowledge, Wake, new selection |
| `FAULT` | latched application or power-board fault | Acknowledge after the power side is safe |

### 2.2 Power-board layer

The lower layer has its own states, including `BOOT`, `STOPPED`, `ARMED`,
`STARTING`, `HEATING`, `ACTIVE_ZERO`, `PAUSED`, `NO_PAN` and `FAULT`. The two state
machines are related but not transactional: a cooking state can change before the
power state has reached the matching physical condition. Most findings below come
from that boundary.

### 2.3 Intended high-level invariants

These invariants should become explicit assertions or executable tests:

- `IDLE`, `SLEEP`, `COMPLETE` and acknowledged-fault states imply a physically
  confirmed zero-output power board.
- A live cooking state must renew a bounded command lease; an expired lease forces
  repeated Stop commands even if the last requested gear was nonzero.
- Only one transition may own the power board at a time.
- A command reported as successful must either be physically confirmed or clearly
  reported as pending.
- Pause, NoPan and active-zero coast retain the session but must never reuse stale
  output, timers or sensor history unexpectedly.
- A setting change immediately followed by Start must start with that exact setting.
- A delayed Start must remain cancelable and must not be silently destroyed by menu
  navigation.
- All user actions must have a defined result in every state: accepted, queued,
  ignored with feedback, or rejected with feedback.

## 3. Critical findings

### C1. Stop is declared complete before physical Stop is confirmed

Evidence:

- `normal_stop_locked()` calls `powerboard_control_stop()` and immediately changes
  the cooking state to `IDLE` or `COMPLETE`.
- `powerboard_control_stop()` changes the lower state and wakes the control task, but
  the actual I2C Stop sequence is transmitted asynchronously.
- Communication-loss escalation deliberately excludes `PB_STATE_STOPPED`.

Why this matters:

The UI and higher-level state can say that cooking has stopped while one or more Stop
transactions are still pending. The power task normally repeats Stop when required,
and `STOP VERIFY` catches persistent nonzero `R26`, so this is not equivalent to
"Stop is never sent." The problem is the missing transactional guarantee and the
smaller diagnostic window after the software declares itself stopped.

Recommended change:

- Add `COOK_STATE_STOPPING` and, if useful, a distinct lower-layer stopping phase.
- Enter `STOPPING` after any user Stop, timer completion, profile completion, fault,
  pause timeout or hard limit.
- Continue Stop transmissions and I2C-loss accounting while stopping.
- Enter `IDLE`/`COMPLETE` only after valid feedback confirms `R26 == 0` for a chosen
  number of consecutive samples.
- If confirmation times out or communication is lost, keep forcing Stop and latch a
  diagnostic fault whose original cause is preserved.
- Define which screen and buttons are active during `STOPPING`; a second Stop should
  be idempotent.

Acceptance tests:

- Inject I2C failures immediately before, during and after Stop.
- Hold `R26 != 0` and verify the UI never announces a completed Stop.
- Recover I2C during `STOPPING` and verify normal completion.
- Repeat for user Stop, Cancel, timer completion, profile completion and fault.

### C2. No higher-level cooking-command lease exists

Evidence:

- The power-control task is registered with the ESP task watchdog and resets it each
  control cycle.
- The cooking task is not independently supervised by the power task.
- The power task owns the last target gear and can continue its normal 500 ms command
  loop without receiving fresh decisions from the cooking task.

Why this matters:

If the cooking task deadlocks, starves or is corrupted while the lower power task
continues running, the hardware watchdog sees the power task as healthy. Heating can
therefore continue at the last target until another lower-layer limit, the five-hour
deadline, or the power board's own native protection intervenes.

Recommended change:

- Add a monotonically increasing cooking heartbeat or lease token.
- Require renewal faster than a conservative deadline, for example every 500–1000 ms.
- Let only the power task evaluate lease expiry; it must force repeated Stop without
  waiting for the cooking task.
- Renew the lease in deliberate `ACTIVE_ZERO`, `PAUSED`, `NO_PAN` and scheduled live
  states as well, but never from an unrelated UI/network task.
- Keep the existing power-task watchdog. The lease solves a different failure mode.
- Log one compact transition on lease expiry and preserve it as the primary cause.

Acceptance tests:

- Suspend the cooking task while heating and verify bounded Stop latency.
- Suspend the UI task only and verify cooking continues.
- Suspend the power task and verify the existing task watchdog still resets/stops the
  system according to the release policy.

## 4. High-priority findings

### H1. Queued Set operations can race with synchronous Start

`cooking_set_mode()`, `cooking_set_power()` and `cooking_set_temperature()` post
asynchronous intents. `cooking_start()` bypasses that queue and calls
`begin_run_locked()` synchronously. A fast sequence such as selecting T°C, turning the
encoder and immediately pressing Start can therefore start from the previous mode or
setpoint before the queued intents are consumed.

Recommended change:

- Use one serialized command path. The preferred design is a synchronous
  request/response intent carrying a completion notification.
- Alternatively add one atomic `configure_and_start()` command containing mode,
  target/gear, timer and profile identity.
- The UI must display a warning if Start is rejected or times out; silent failure is
  not acceptable.

### H2. Pan return may resume a stale output

When the lower layer changes from `NO_PAN` back to `STARTING`, its retained target can
resume before the cooking layer has recomputed the correct current temperature output.
In temperature mode, the proper output depends on the latest temperature and trend;
in POWER mode it depends on the latest user selection.

Recommended change:

- Treat pan return as a deliberate transition owned by the cooking layer.
- Keep active zero while recomputing the requested output.
- Reset/observe the temperature controller, calculate the first safe output, update
  the target, then permit heat to resume.
- Apply the topology/ramp rule to the resumed output.

### H3. A temperature change during `STARTING` is applied late

The target field changes immediately, but `update_temperature_locked()` observes and
returns without applying output until the cooking state becomes `COOKING`. A user can
therefore lower the target during startup while the initial output remains based on
the old target.

Recommended change:

- Recompute and apply the safe target during `STARTING`, or explicitly place the
  board into active zero and restart the controlled Start transition.
- Test large downward edits near high-temperature targets.

### H4. A cold Start at active zero is reported as cooking before feedback confirms it

If the initial temperature output is zero, `begin_run_locked()` immediately selects
`COOK_STATE_COOKING`, while the lower layer sets `PB_STATE_ACTIVE_ZERO` as soon as the
command is accepted internally. There is no separate physical confirmation that the
retained-session zero command was accepted by the external board.

Recommended change:

- Introduce a pending active-zero start state or reuse a generalized `STARTING` state.
- Confirm the expected `R20`/`R26` response pattern before reporting a retained live
  zero session.
- Define a timeout and a fallback repeated Stop path for an unrecognized response.

### H5. Active-zero Set does not have a dedicated health/confirmation contract

`powerboard_control_set_gear(0)` updates the lower state immediately. A successful
function return means the request was accepted by software, not that the next I2C
cycle and feedback proved active zero on the external board.

Recommended change:

- Separate `requested_gear`, `transmitted_gear` and `confirmed_gear/session_state`.
- Keep a transition deadline and confirm expected feedback.
- Expose pending versus confirmed state in compact UART diagnostics and in the
  authenticated status endpoint.

### H6. The five-hour lower-layer deadline conflicts with retained zero and long profiles

The power-board run deadline starts once in `powerboard_control_start()` and continues
through `ACTIVE_ZERO`, manual Pause and profile gear-zero wait stages. The requested
behavior says manual Pause stops after two hours, while ordinary POWER zero and
profile zero are legitimate non-heating waits and should not be treated as Pause.
Nevertheless, all of them currently consume the same five-hour run budget.

Recommended change:

- Decide and document separate policies for energized heating time, retained-session
  wall time, manual Pause time and profile wait time.
- Never remove the final safety bound without replacing it with a clearer bound.
- A robust option is a renewable lower-layer lease plus a separately accumulated
  maximum heating-on duration. Profile wait time can then be long without permitting
  stale nonzero commands.

## 5. Medium-priority findings

### M1. Mode/profile selection can silently break a delayed Start

`INTENT_SET_MODE` is accepted in `DELAYED` because that state is not classified as
active. It changes the cooking state to `READY` but does not consistently clear the
delayed fields. Profile selection has the same conceptual problem. The schedule
updater only advances when the state is exactly `DELAYED`, so the scheduled Start can
be left logically set but no longer executable.

Recommended change:

- While `DELAYED`, either reject mode/profile edits with user feedback or edit the
  schedule's stored recipe atomically while remaining in `DELAYED`.
- Never leave `delayed_start == true` outside a valid delayed-transition context.

### M2. Pausing from NoPan retains the old NoPan timestamp

The Pause path stops the NoPan sound, but it does not clearly reset
`s_no_pan_since_us`. After Resume, a still-missing pan can inherit the previous elapsed
time and reach E02 earlier than the user expects.

Recommended change:

- Specify policy: Pause should freeze the NoPan timer or reset it.
- Store the remaining time explicitly if freezing is desired.
- Reset all announcement flags and timestamps together in one transition helper.

### M3. Very early Pause/Resume can leave the user in Pause with only a silent rejection

Pause can be accepted during `STARTING`. If Resume follows before feedback establishes
the retained session (`R26 != 0`), `retained_session_healthy_locked()` rejects Resume.
The state stays paused and the UI currently gives no clear rejection reason.

Recommended change:

- Add `PAUSING` and `RESUMING` confirmation states or delay the Pause acknowledgement
  until the retained session is known.
- Surface `resume pending`, `resume rejected`, and the exact compact reason.
- Make repeated short presses idempotent rather than queueing contradictory toggles.

### M4. Partial Start failure can temporarily leave the lower layer armed

`begin_run_locked()` calls Arm and then Start. If Arm succeeds but Start fails, the
function returns an error without explicitly disarming. The lower layer eventually
expires its arm window, but the operation is not atomic.

Recommended change:

- On any failure after Arm, force and confirm Stop before returning.
- Prefer one lower-layer `arm_and_start()` transaction with a single cleanup path.

### M5. Fault visibility while stopped is intentionally weaker

Consecutive I2C failure becomes E09 only when the lower state is neither `STOPPED` nor
already `FAULT`. This avoids nuisance E09 while the cooker is idle, but it can also
hide a persistent communication fault until the next Start attempt.

Recommended change:

- Keep heating faults and idle diagnostics separate.
- While stopped, expose a non-latched `communication unavailable` readiness state.
- Block Start with a specific reason after failed preflight rather than a generic
  `START BLOCKED` message.

### M6. A scheduled run leaves the UI view unsuitable for live encoder adjustment

When the delayed Start becomes due, the cooking state goes live but the UI's internal
view can remain `VIEW_HOME`. The live display overlay is cleared, yet encoder events
are still interpreted by `VIEW_HOME`, so rotation changes the menu selection instead
of the running POWER level or temperature setpoint.

Recommended change:

- On live-state entry, synchronize the UI view with the effective running mode.
- For profiles, define whether encoder edits are forbidden or temporarily override a
  stage, and show that rule explicitly.

### M7. Long center in the timer editor closes the editor instead of stopping cooking

`central_long()` first handles `s_timer_editing`, so a long center press during an
active timer edit closes the editor. It does not execute the otherwise documented
long-press Stop action.

Recommended change:

- Give emergency/user Stop priority over editor navigation while cooking.
- Use a short press or Cancel gesture to close the editor.
- Add a complete input-priority table for every live overlay.

### M8. Resume from active zero bypasses the normal ramp

`powerboard_control_set_gear()` changes `ACTIVE_ZERO` directly to `STARTING` with the
new gear already applied. Manual Resume similarly restores its target directly. This
bypasses `next_ramped_gear()` and can make a large jump, including to high topology.
The direct jump may be correct for the stock protocol, but it needs deliberate
validation and should not happen merely as a side effect of state assignment.

Recommended change:

- Decide whether retained-session resume must use a ramp, a bounded first step, or a
  stock-derived immediate command.
- Preserve the current direct 35/56 topology boundary rule.
- Test zero-to-low, zero-to-high, Pause-to-low and Pause-to-high transitions while
  logging relays, fan, `R20`, `R26` and the transmitted commands.

## 6. Lower-priority consistency findings

These are unlikely to be immediate safety failures, but fixing them will make future
diagnosis and testing more trustworthy.

### L1. `applied_gear` can describe intent before physical application

Both layers sometimes assign `applied_gear` when a target is accepted, before the
ramp or feedback proves that exact gear. Rename fields or split them into requested,
transmitted and confirmed values.

### L2. `run_elapsed_s` can remain stale after Stop

The cooking-layer timestamp is not consistently reset in every terminal transition.
Reset it on confirmed Stop or explicitly retain it as `last_run_elapsed_s`.

### L3. `paused_gear` can become stale across unusual transitions

Centralize entry/exit actions for Pause, Stop, Fault, NoPan and profile-stage changes
so `paused_gear` is never left over from a previous context.

### L4. `pan_present` is partly inferred from power-board state

Outside the special scheduled-wait path, pan presence is often derived from
`pb->state != PB_STATE_NO_PAN`, not directly from a valid current reading. Represent
it as `unknown / present / absent` when I2C data is missing.

### L5. Temperature trend samples do not carry individual timestamps

The trend window is sample-based. After a communication gap, old and new samples can
be treated as if evenly spaced. Timestamp samples or reset the trend window after a
bad cycle/gap.

### L6. Saturation notification lifetime is ambiguous

The one-shot announcement flag is reset on some transitions but not expressed as an
explicit policy. Define when a new saturation episode begins, what clears it, and
whether the user must acknowledge it.

### L7. Manual NoPan and scheduled NoPan converge through different setup paths

Scheduled Start has `s_waiting_pan_before_start`, whereas live pan loss uses the
normal lower-state path. Consolidate shared timeout, sound and recovery actions to
avoid future drift.

### L8. Queue success means enqueue success, not operation success

Most public cooking APIs return success when the intent enters the queue. Callers can
therefore play a confirmation click even if the operation is later rejected. Add
operation completion/result reporting for actions where the user needs certainty.

## 7. Temperature-controller review

The reviewed controller has several good properties that should be preserved:

- the selectable range is 40–190 °C;
- interface-side overtemperature protection is separate from the target setpoint;
- initial heating uses an adaptive braking margin based on recent positive rise;
- high targets receive additional braking reserve;
- phase hysteresis prevents PREHEAT/APPROACH chatter;
- automatic regulation crosses directly between `1…35` and `56…99`, avoiding
  transient automatic commands in `36…55`;
- HOLD is capped independently;
- at/above target, the requested output can be true active zero;
- Pause observes temperature without integrating PI, then recomputes on Resume.

Remaining controller risks are mostly transition-related rather than ordinary HOLD
tuning:

- trend history after sensor gaps;
- stale initial output after pan return or target edits during `STARTING`;
- direct high-power resume from active zero;
- the interaction between adaptive braking, an empty pan and different thermal loads;
- saturation messaging and timer semantics while output is capped.

Do not retune the stable HOLD response until transition faults are separated from
steady-state tuning. Field evidence already showed approximately 5 °C initial
overshoot at a 125 °C empty-pan target while subsequent holding was accurate; the
adaptive braking change remains to be characterized on hardware.

## 8. User-action matrix to specify and test

For every cell, define: accepted/rejected, resulting state, power command, timer
effect, audible/visual feedback, and whether the action is idempotent.

| Current context | Rotate encoder | Short center | Long center | Cancel | Timer key |
| --- | --- | --- | --- | --- | --- |
| Sleep | Wake only; guard first detents | Wake only | Wake policy | Wake/cancel policy | Wake only |
| Idle/Ready | Navigate or edit | Select/Start | Sleep or context action | Home | Open timer |
| Delayed | Defined schedule edit policy | Defined view action | Cancel delayed Start | Cancel delayed Start | Timer policy |
| Starting | Edit active target safely | Pause request | Stop | Stop | Timer editor |
| Cooking | Edit active target | Pause | Stop even over editors | Stop | Timer editor |
| Active-zero cooking | Edit target | Pause | Stop | Stop | Timer editor |
| Paused | Edit resume target | Resume | Stop | Stop | Timer policy |
| NoPan | Edit target policy | Pause | Stop | Stop | Timer policy |
| Stopping | Ignore or queue | Idempotent Stop | Idempotent Stop | Idempotent Stop | Ignore |
| Complete | Navigate after acknowledge | Acknowledge | Home/Sleep policy | Acknowledge | Ignore |
| Fault | No mutation | Acknowledge only if safe | Acknowledge policy | Acknowledge policy | Ignore |

Special sequences that require explicit tests:

1. mode select → immediate Start;
2. setpoint turn → immediate Start;
3. Start → immediate Pause → immediate Resume;
4. Start above current setpoint and Start below current temperature;
5. lower the setpoint sharply during `STARTING`;
6. remove pan during PREHEAT, APPROACH, HOLD and active zero;
7. Pause while NoPan, wait, Resume with and without pan;
8. schedule Start, then enter every menu and change mode/profile;
9. scheduled Start → encoder adjustment;
10. timer editor open → long center Stop;
11. active zero → high power and Pause → high power;
12. Stop while I2C fails, then recover I2C;
13. cooking-task stall while the power task remains healthy;
14. profile with long gear-zero stages crossing the five-hour boundary;
15. every native and interface-generated fault before/during/after a transition.

## 9. Test gaps

The current static safety checks are valuable regression gates, but they mainly prove
that selected code patterns exist. They do not execute the interacting state machines.
The missing layer is a deterministic host-side model or test harness with injectable
time, I2C replies and input events.

Required automated coverage:

- a transition table that enumerates every cooking state and intent;
- lower-layer command/feedback simulation for `R20`, `R26`, read failures and delays;
- queue ordering and immediate button-sequence tests;
- timer, Pause and schedule tests with virtual time;
- invariant checks after every transition;
- temperature-controller tests with rising, falling, noisy and interrupted samples;
- fault precedence tests proving the first cause remains latched;
- fuzz/property tests over valid input sequences;
- a build-time rule preventing remote heat-start capabilities from being introduced.

Hardware tests remain necessary for relay/fan behavior, unknown stock responses,
active-zero retention, topology changes and thermal overshoot. Software simulation is
not a replacement for a supervised cookware test.

## 10. Ordered implementation plan

### Phase 0 — Freeze behavior and add observability

- [ ] Record the exact current transition diagrams and UART frame definitions.
- [ ] Add host-side mocks for time, input events and power-board feedback.
- [ ] Convert the invariants in section 2.3 into failing tests.
- [ ] Preserve compact transport while keeping full diagnostic coverage.

### Phase 1 — Make Stop transactional

- [ ] Add `STOPPING`.
- [ ] Make every terminal path request the same idempotent Stop transaction.
- [ ] Confirm zero feedback before `IDLE`/`COMPLETE`/acknowledged Fault.
- [ ] Keep communication and Stop-verification faults active while stopping.
- [ ] Test all Stop origins and I2C fault injection.

### Phase 2 — Add the cooking lease

- [ ] Add lease generation/renewal at the cooking-to-power boundary.
- [ ] Force repeated Stop on expiry from the power task.
- [ ] Distinguish cooking-task failure from power-task watchdog failure.
- [ ] Test deliberate task suspension.

### Phase 3 — Serialize commands and transitions

- [ ] Replace mixed asynchronous Set/synchronous Start behavior.
- [ ] Add explicit completion results and UI feedback.
- [ ] Make Arm+Start atomic with cleanup.
- [ ] Introduce pending confirmation for Start, active zero, Pause and Resume.

### Phase 4 — Repair edge-state context

- [ ] Centralize entry/exit helpers for Pause, NoPan, Fault and Stop.
- [ ] Reset/recompute timers, trend, saturation flags and requested output explicitly.
- [ ] Recompute output before pan return and Resume.
- [ ] Define ramp behavior from retained zero.

### Phase 5 — Resolve timers and delayed Start

- [ ] Separate heating time, session time, manual Pause time and profile wait time.
- [ ] Make delayed configuration immutable or atomically editable.
- [ ] Synchronize the UI view when a scheduled run begins.
- [ ] Give long-center Stop priority over live editors.

### Phase 6 — Field validation

- [ ] Build and run all offline gates.
- [ ] Review the exact binary and flash range; do not flash without explicit approval.
- [ ] Perform no-heat UI/I2C transition testing first.
- [ ] Perform short water-load POWER and TEMPERATURE tests.
- [ ] Test NoPan, Pause/Resume, active zero and Stop confirmation.
- [ ] Characterize high-temperature braking with conservative supervised steps.
- [ ] Update this checklist with measured evidence, firmware version and logs.

## 11. Definition of done

This plan is complete only when:

- every state/intent pair has a documented result;
- `IDLE`/`COMPLETE` never precede confirmed physical Stop;
- a stalled cooking task causes bounded autonomous Stop;
- configuration followed immediately by Start is deterministic;
- Pause/Resume and NoPan recovery cannot reuse stale output or time;
- delayed Start remains internally consistent through all permitted UI actions;
- active-zero and long-wait policies are explicit and tested;
- all offline gates pass and the hardware transition matrix is completed under
  supervision;
- the changelog and development context identify which findings were actually fixed;
- flashing remains a separate, explicit owner-authorized action.

