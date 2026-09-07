# Follow-up audit: false faults around zero-output waits

Source: `0.2.35-dev`, 2026-09-07. Both production flavors share these fixes.
Status: offline C scenarios and builds; this audit build has not been flashed.

## Field evidence and scope

The captured hot-temperature Start sent `W0D/W00/W0C=81/00/00`, received
`R20=00, R26=00` with valid feedback, and then raised `START TIMEOUT`. The
interface had requested no heat but required an active-session acknowledgement.
The immediate `0.2.34-dev-private` patch accepted output-off feedback only for
zero Start. This follow-up checks the resulting full Pause/Resume/NoPan/Stop
chain, including cases the earlier separate Python models did not represent.

The source review covered every `set_fault_locked`, `fault_locked`, and transition
timeout origin in the cooking engine and power-board driver, plus physical center,
Cancel and timer dispatch. Findings below are demonstrated source paths, not a
claim that every scenario has occurred on the physical cooker.

## Confirmed defects and fixes

| Scenario | Previous behavior | Correction |
| --- | --- | --- |
| Start above the temperature setpoint, then Pause | The initial zero Start completed, but Pause still required `R26=01/02`; `00` caused `PAUSE ACK TIMEOUT` / EPB | Every zero-output transition accepts two fresh consecutive compatible `R26=00` responses, as well as the existing retained-session response |
| Resume from a Pause entered while waiting for cooling | `retained_session_issue_locked()` rejected `R26=00`, leaving Resume blocked | A confirmed zero-output Pause/wait with a successfully transmitted zero command may Resume; output-off still cannot confirm nonzero heating |
| Lower the target while nonzero Start is still pending | The urgent zero-Start patch rejected retained `R26=01/02` feedback and could cause another EST | Zero Start accepts both the off and retained-session paths |
| First real heating after a cold zero wait | Resume assumed heat had already run: it skipped the cold ramp and allowed only three seconds for acknowledgement | Track whether nonzero heat was confirmed; first heat uses the target-topology cold ramp and eight-second acknowledgement window; retained Resume keeps its existing behavior |
| Edit a target during Start | The replacement path still began at gear 10 even for middle/high topology targets | Replacement Start uses the same target-topology ramp as ordinary Start |
| Return a pan hot enough to need zero output | Pan-return hold/zero Resume could demand an energized-session status; pan loss just after a completed hold could leave `PAN RETURN WAIT` stuck | Accept confirmed off feedback for these zero transitions and detect renewed NoPan while waiting to recompute output |
| Edit temperature while the lower task independently enters pan-return hold | The rejected overlapping request became `TEMP UPDATE FAILED` / EPB | Retain the new setpoint for the already-zero pan-return path to recompute; let an actual lower fault/Stop retain its own cause |
| Profile cell expires during a lower output/pan transition | A rejected next-cell command could become `PROFILE STAGE FAILED` / EPB | Wait for a settled lower state; roll back an overlapping cell change and retry without skipping the cell |
| A completion arrives after Stop/Fault or after a newer transition | An old confirmation could set the cooking state back to COOKING | Consume only the current completed generation, matching lower state, in a still-active upper state |
| First scheduled Start times out | The lower fault correctly cancelled its lease, but the engine could treat that as a new lease failure before Stop completed, destroying the retry | Preserve the lower Start fault/retry path while waiting for Stop; the second attempt can proceed |
| A later failure occurs while stopping after an earlier error | A different upper error could replace the original error code | Preserve the first upper fault until acknowledgement |
| Invalid feedback interrupts a confirmation sequence | Stop and NoPan could count samples on both sides of the read gap | Break the candidate sequence on invalid feedback |
| One high IGBT reading is copied by several 100-ms engine ticks | The advisory's two-sample filter counted copies of one reading | Count distinct lower IGBT read attempts; the warning still clears on Stop and follows the existing temperature thresholds |
| Temperature output is awaiting lower confirmation | The controller overwrote the lower applied value with the requested gear | Keep the lower driver's reported/applied value during the transition |

## Verification

`firmware/production/tests/host_scenarios.py` compiles the actual C files for the
engine, power driver and temperature controller. Only the RTOS, clock, sound,
settings and hardware transport are stubbed. Each scenario runs in a separate
process, avoiding shared state between tests. Fixtures supply complete successful
command transmission and deliberate feedback/interleavings; they do not simulate
electrical I2C timing, relay mechanics or the stock power MCU internally.

The 25 scenarios cover:

- prolonged cooling, Pause, zero Resume, positive Resume, and first heating;
- target edits during Start and during pan return, including topology boundaries;
- pan-return off hold and loss of cookware immediately after the hold;
- profile-stage overlap, zero-power timer completion and the two-hour manual Pause;
- successful delayed retry and EST after the second unconfirmed attempt;
- stale confirmations after Stop/Fault and preservation of the first error;
- interrupted Stop/NoPan/zero confirmation sequences and distinct IGBT samples;
- actual missing heating acknowledgement, native E07, sustained critical I2C loss
  and a genuine expired cooking lease, which must still stop the session.

Run with native GCC/Clang:

```sh
python firmware/production/tests/host_scenarios.py
```

Or provide a native Zig compiler with `--zig /path/to/zig`. The locally installed
test compiler is under ignored `_local_private/tools/ziglang/`; no compiler,
private melody or firmware binary is added to Git.

The existing policy, localization and source safety gates remain additional
checks. They are not substitutes for executing the C paths.

## Remaining limits and uncertainties

- A confirmed cooling wait has no Start timeout. The existing eight-hour overall
  session limit and an explicitly enabled cooking timer still apply. This change
  does not promise an unlimited wall-clock session.
- Real nonzero Start still needs `R26=01/02`; the initial deadline is eight
  seconds. A confirmed retained-session output transition has its existing
  three-second deadline. Missing/unknown hardware acknowledgement is still a
  fault, not automatically proof that heating succeeded.
- Native fault codes, 98 °C IGBT/210 °C bottom cutoffs, the stopped-IGBT 80 °C
  Start gate, communication time limits and the cooking lease retain the agreed
  policy. See [the limit inventory](PRODUCTION_LIMITS_AND_AUTOMATIC_STOPS.md).
- Production's handling of an interrupted temperature read still retains the last
  output during the existing bounded I2C recovery interval. This remains an owner-
  visible policy, not a new change made by this audit.
- R21 semantics, cookware-dependent P1 relay behavior and measured low-power
  differences remain experimental questions. This audit does not reinterpret
  undocumented registers or change the stock-derived power command encoding.
- Fresh source-level scenarios cannot establish that every response from the
  physical power MCU is known. A supervised hot Start/Pause/Resume/pan-return run
  with UART is the remaining device check for this build.
