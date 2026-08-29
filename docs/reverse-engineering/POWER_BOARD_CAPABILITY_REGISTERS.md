# Power-board capability registers `R25`, `R28`, and `R29`

This note preserves the current evidence and the deferred compatibility decision for
three startup registers. They are not ordinary cooking status codes and must not be
confused with values carried by `R20`. In particular, register `R29` and the service
event value `R20=29` are unrelated protocol objects.

## Current evidence

| Register | Evidence from the stock interface firmware and captures | Current custom behavior | Deferred decision |
|---|---|---|---|
| `R25` | Startup capability/version byte. It was consistently `0x0A` in the existing capture, and stock initialization expects the low nibble to be `0x0A`. | Read at startup and during the ordinary heartbeat; retained in raw diagnostics, but not used to select control behavior. | Compare the complete byte on another power-board revision. Do not reject a board or change heating policy until supported and unsupported values are known. |
| `R28` | Power-board type/revision selector. Stock code uses values greater than `2` to select its newer lookup-table conversion path for both `R23` IGBT and `R24` bottom NTC channels. | Read at startup, retained in diagnostics, and shown as raw `R28 XX` in the physical firmware-information screen. Production currently always uses the extracted newer-board lookup tables. | Before supporting a genuinely different board revision, either select a verified conversion family from `R28` or block Start with an explicit unsupported-board reason. Never silently apply an unverified NTC conversion. |
| `R29` | Companion revision/capability flag read together with `R28`; stock retries the pair up to five times. A nonzero value enables a stock cross-channel temperature-difference check. | Read once during startup and retained in the register snapshot. It does not currently change cooking or safety policy. | Capture both cold-idle and heated values on each distinct board revision, then reproduce the stock check only after its thresholds and sensor relationship are verified. |

## Why no new runtime action is added now

The second cooker is expected to be the same model and power-board revision, so no
evidence currently justifies a speculative compatibility branch. The safe present
policy is to preserve the raw values, keep `R28` visible, and continue using the
already validated conversion path on the tested hardware. A different value is a
compatibility investigation trigger, not an automatically invented fault meaning.

When another cooker is available, record one startup tuple before supervised heating:

```text
R25=XX R28=XX R29=XX R23=XX R24=XX
```

Also record the firmware hash and whether the readings were taken cold or hot. If the
tuple matches the tested board, no new behavior may be necessary. If it differs, the
`R28` NTC-family decision is mandatory before heating; `R25` and `R29` remain
capability evidence until their stock behavior is reproduced with deterministic
tests.

## Related deferred work

This is tracked as Package 2B in the
[state-machine implementation plan](../STATE_MACHINE_IMPLEMENTATION_PLAN.md). The
stock-code evidence and register-by-register audit are in
[Stock power-board response audit](STOCK_POWER_BOARD_RESPONSE_AUDIT.md).
