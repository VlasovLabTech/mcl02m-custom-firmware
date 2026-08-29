# Stock Power-Board Response Audit

Date: 2026-08-29
Stock panel firmware: `2.2.0_0016`

## Scope and evidence

This audit re-traces every power-board register consumed by the stock ESP32 panel
firmware. It combines the preserved stock binary, Ghidra decompilation, the recovered
`CookStepTask` and `Period250MsTask`, and existing passive I2C captures.

The separate power-MCU firmware is not present in the flash dump. Consequently, the
stock panel binary proves how the panel reacts to values it recognizes, but cannot
enumerate every value that every power-board revision could ever produce.

Primary evidence:

- `_local_private/reverse-engineering/work/cookstep_hidden.c`
- `_local_private/reverse-engineering/work/period250_hidden.c`
- `_local_private/reverse-engineering/work/excerpts_40153f24.c`
- `_local_private/reverse-engineering/work/excerpts_40154b20.c`
- `_local_private/reverse-engineering/work/ghidra_decompile/all_functions.c`
- `_local_private/diagnostics/raw-captures/i2c_2026-08-23_stock_startup_sequence/`

## Main conclusion

`R26=01` is a valid heating acknowledgement with a restricted cookware capability.
The stock panel immediately caps the requested gear at decimal 35 and forces the
`0x80` ring selection, encoded as the `A1` command topology. `R26=02` is the
unrestricted heating acknowledgement. This is a control input, not an error code.

The live comparison between small and large cookware strongly supports the practical
interpretation below:

| `R26` | Stock interpretation | Stock reaction |
|---:|---|---|
| `00` | output inactive or not yet acknowledged | remain off/starting according to the surrounding state |
| `01` | restricted cookware capability, consistent with small cookware | accept heating, cap gear at 35, force `A1` |
| `02` | unrestricted cookware capability | accept heating and retain the normal requested gear/topology |

The exact physical names of the individual `A1` and `C1` rings remain unproven, but
`A1` is definitively the stock topology used for the restricted state.

## `R20` status handling in the stock panel

The stock fault evaluator is `FUN_40153f24`. Ordinary persistent faults use separate
counters and latch only after more than ten matching samples. No-pan uses a separate
six-sample path. A single sample is therefore not a stock fault decision.

| `R20` | Stock reaction | Current confidence |
|---:|---|---|
| `00` | normal status; resets the relevant transient counters | high |
| `02` | no cookware; enters the stock no-pan path after six matching samples | high |
| `0B` | persistent high-voltage fault path | high |
| `0C` | persistent low-voltage fault path | high |
| `15`, `16`, `18` | persistent sensor-fault group | high |
| `17` | persistent IGBT overtemperature group | high |
| `19`, `1A`, `1C`, `1D` | persistent wire/channel fault group | high |
| `1B` | bottom-temperature fault path, shared with converted NTC limits | high |
| `29` | immediate service/event notification; increments a service counter in calibration mode | high for behavior, low for physical name |
| `2A` | second immediate service/event notification; increments the paired service counter | high for behavior, low for physical name |
| `01` | persistent internal stock fault number 12 | high |
| `2B` | no explicit stock fault branch; runtime captures identify it as a relay-transition status | high |
| other values | no explicit fault branch was found in this stock panel build | high for this binary only |

The custom driver currently maps the known persistent fault groups correctly, but its
filter is intentionally much shorter than stock. It also currently treats `29`, `2A`,
and any other unlisted nonzero value as an eventual generic `EPB`, unlike the stock
panel. This is a compatibility gap and should be addressed separately from the
`R26=01` cookware fix.

## Register-by-register audit

| Register | Stock use | Control significance | Remaining custom-firmware gap |
|---:|---|---|---|
| `20` | status and fault classifier | critical | service values `29/2A`, generic-unknown policy, and debounce differ from stock |
| `21` | load/current-like feedback; displayed and range-checked only in service diagnostics | diagnostic | retain raw telemetry; do not use as the primary Start acknowledgement |
| `22` | mains input raw value; displayed as raw + 50 and checked against the service range | diagnostic/safety | current voltage conversion is correct; normal stock cooking still relies primarily on `R20` fault status |
| `23` | IGBT NTC raw; converted through the stock LUT on newer board types | safety/control | custom firmware always applies the newer-board LUT and must account for the `R28` board-type rule |
| `24` | bottom NTC raw; converted through the stock LUT on newer board types | safety/control | same board-type compatibility issue as `R23` |
| `25` | startup capability/version byte; low nibble is expected to be `0A` by stock service initialization | startup compatibility | read but not interpreted |
| `26` | cookware/output capability feedback | critical control | addressed by the 2026-08-29 gear-35/`A1` implementation |
| `27` | auxiliary feedback shown in stock service pages as `B`; observed values include `00/01/02` | diagnostic only in stock | exact physical meaning remains unknown; it must stay visible in telemetry |
| `28` | power-board type/revision selector; values above 2 enable both NTC lookup conversions | startup compatibility | read once but not used to select conversion policy |
| `29` | companion revision/capability flag; nonzero enables a stock cross-channel temperature-difference check | startup compatibility | read once but not used |
| `2A`, `2B` | calibration/test parameters used as commanded gears in two stock factory-service modes | service only | no production behavior is required, but startup must not misclassify them as ordinary status bytes |
| `2C`–`2F` | service values displayed as `C`, `D`, `E`, and `F` only on the stock diagnostic page | service only | custom startup unnecessarily requires all four reads to succeed |

The stock diagnostic page labels the groups as `A` through `F`, followed by `I` and
`V`: `A=R26`, `B=R27`, `C=R2C`, `D=R2D`, `E=R2E`, `F=R2F`, `I=R21`, and `V=R22`.
No normal cooking branch consumes `R27` or `R2C` through `R2F`.

## Implementation priorities discovered by this audit

1. **Implemented now:** accept `R26=01` as heating, enforce gear 35 and `A1` in
   every mode, show the real permitted power, and explain blocked upward edits.
2. **High priority:** whitelist or explicitly handle the stock service statuses
   `R20=29/2A` so they cannot become a false `EPB`.
3. **High priority:** replace the broad one-second “unknown nonzero means fault” rule
   with a documented policy that distinguishes known stock faults, confirmed service
   statuses, relay transitions, and truly unknown values.
4. **High priority for the second cooker:** honor `R28` before selecting the NTC
   conversion method, and validate `R25/R28/R29` as board capabilities rather than
   assuming the first cooker revision.
5. **Medium priority:** stop making boot success depend on `R2C`–`R2F`; stock reads
   these registers only inside its service page.
6. **Diagnostic only:** keep `R21`, `R22`, `R27`, and `R2C`–`R2F` observable. Their
   exact physical semantics do not justify new control actions without runtime data.

## What still requires physical capture

Static analysis cannot prove the physical inner/outer assignment of `A1` versus
`C1`, the power MCU's complete value space, or the exact meaning of `R27` and the
factory-service channels. Those questions require a passive I2C capture or an
equivalent power/thermal correlation on the relevant cookware and board revision.
They are not blockers for the restricted-cookware policy because that policy is
explicit in the stock ESP32 code.
