# Stock `R21` analysis and cookware power-sweep plan

Date: 2026-09-03
Stock panel firmware: `2.2.0_0016`

## Executive conclusion

`R21` is a one-byte measurement returned by the power board. It strongly follows
delivered load, but its exact physical quantity and scale have not been proven.
It is not the stock panel's heating acknowledgement, relay acknowledgement, or
normal cooking safety input. The stock panel stores every checksum-valid value,
shows it as `I%03d` on a service page, and range-checks it only in a factory/service
test. Normal cooking decisions do not consume it.

Runtime evidence also rules out treating `R21` as a globally linear power, current,
or watt reading. Its values change with commanded gear, active topology, cookware,
and the power board's duty cycle. In particular, the `35 -> 36` and `55 -> 56`
boundaries change the command topology, and `R21` is not monotonic across every
boundary.

Confidence:

- **High:** how stock reads, validates, stores, displays, and service-checks `R21`.
- **High:** `R21` is not used by the normal cooking control branch in this stock
  binary.
- **High:** it is correlated with delivered load and falls near idle values during
  NoPan.
- **Medium:** the stock `I` label means current-like feedback.
- **Unknown:** physical units, transfer function, filtering inside the power MCU,
  and whether every power-board revision uses the same scale.

## Static firmware evidence

The recovered `CookStepTask` is `FUN_401542b0`. Its normal 500 ms cycle reads:

```text
R20, R21, R22, R23, R24, R25, R26, R27, W0D, W00, W0C
```

The `R21` branch calls the stock I2C read routine with selector `0x21`, validates
the returned checksum as `value + 0x21`, and stores the byte in the 16-bit slot at
`0x3ffcf2a2`. If the checksum is invalid, the store is skipped, so the previous
valid value remains available. There is no call from this branch to the stock
fault evaluator and no gear, topology, relay, or state transition depends on it.

The only additional consumers found across the decompiled application are service
and factory UI paths:

1. A service test accepts raw values `1...60`. An out-of-range value reports
   internal service-test item `0x0C`. This check is inside the diagnostic workflow,
   not the normal cooking state machine.
2. A service page formats the value with the literal `I%03d`.
3. Another service page renders the same raw value beside voltage and temperature
   measurements.

`R26`, by contrast, is the early digital heating/cookware acknowledgement consumed
by control logic. Existing startup captures show `R26=02` approximately 2.1-3.6 s
after Start, while the first clear `R21` load rise occurs later. `R27` changes later
still and cannot be an immediate relay acknowledgement either.

Primary private evidence paths:

- `_local_private/reverse-engineering/work/cookstep_hidden.c`
- `_local_private/reverse-engineering/work/period250_hidden.c`
- `_local_private/reverse-engineering/work/incident_hidden.c`
- `_local_private/reverse-engineering/work/ghidra_export/strings.csv`
- `_local_private/reverse-engineering/work/excerpts_40153b7c.c`

## Passive stock-capture analysis

`tools/reverse-engineering/analyze_stock_r21.py` replays every decoded stock
`i2c_report.json`, reconstructs the most recent complete `W0D/W00/W0C` command,
and correlates checksum-valid `R21` replies with stable heating intervals. A sample
is included in the stable table only when:

- `W00=01`;
- `W0C` is nonzero;
- at least five seconds have passed since the last command change;
- `R20=00`;
- `R26` is `01` or `02`.

Across eight captures the audit found:

- 2,488 checksum-valid `R21` replies;
- 102 distinct raw values;
- full observed range `2...148`;
- one incomplete `R21` reply;
- no bad-checksum `R21` replies.

The single anomaly occurred in
`i2c_2026-08-22_pass8_clean_full_sequence` at `112.210480 s`. The read frame
contained only address byte `55`, with no value or checksum. The next 500 ms cycle
recovered. The stock panel did not send Stop and no unusual error response followed.
This matches the static behavior: an invalid reply does not overwrite the last
valid `R21` value and is not independently a cooking fault.

The same decoder also gives the following inventory. Hex values are shown exactly
as returned by the power board:

| Register | Valid samples | Values/range seen in preserved stock captures |
|---:|---:|---|
| `R20` | 2,490 | `00`, `02`, `2B` |
| `R21` | 2,488 | 102 values, decimal `2...148` |
| `R22` | 2,490 | `B6...BC` |
| `R23` | 2,489 | 54 values, `AA...DF` |
| `R24` | 2,491 | 94 values, `8E...EF` |
| `R25` | 2,490 | `0A` in 2,482 samples; eight startup/idle `00` samples |
| `R26` | 2,489 | `00`, `02` in these captures; later live small-pan work proved `01` |
| `R27` | 2,488 | `00`, `01`, `02` |
| `R28` | 1 | `0B` |
| `R29` | 1 | `32` |
| `R2A` | 1 | `1C` |
| `R2B` | 1 | `A4` |
| `R2C...R2F` | 0 | not requested in normal captured operation |

`R21` and `R27` are the two still-unknown live channels that demonstrably change.
`R22...R24` also change, but their voltage/temperature roles are already established.
The startup/service selectors have unknown encodings but were sampled only once in
the passive dataset, so these logs cannot establish whether they ever change at
runtime.

### Stable values already present in stock captures

| Gear | Samples | Captures | Min | P10 | Median | Mean | P90 | Max | `R27` values |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 6 | 1 | 2 | 17.0 | 35.0 | 29.67 | 37.0 | 37 | `00` |
| 4 | 2 | 1 | 32 | 32.2 | 33.0 | 33.00 | 33.8 | 34 | `00` |
| 15 | 189 | 2 | 7 | 48.0 | 49.0 | 50.28 | 53.0 | 56 | `00/01` |
| 24 | 79 | 1 | 61 | 61.0 | 62.0 | 61.71 | 62.0 | 66 | `00/01` |
| 30 | 136 | 2 | 61 | 62.0 | 63.0 | 63.67 | 65.0 | 66 | `01` |
| 35 | 202 | 3 | 62 | 69.0 | 70.0 | 70.01 | 71.0 | 77 | `00/01` |
| 36 | 9 | 1 | 76 | 76.0 | 77.0 | 76.78 | 77.0 | 77 | `00` |
| 55 | 96 | 3 | 70 | 72.0 | 73.0 | 73.66 | 78.0 | 78 | `02` |
| 56 | 68 | 3 | 92 | 92.0 | 93.0 | 93.49 | 94.0 | 105 | `00/01` |
| 99 | 81 | 3 | 131 | 133.0 | 141.0 | 138.74 | 145.0 | 146 | `00/01/02` |

The P1 and P4 groups are too short for a reliable cookware comparison. The P15
minimum is an isolated low sample, while its central range is 48-53. Medians and
percentiles are therefore more useful than individual extrema. The machine-readable
result is in [stock_r21_by_power.csv](stock_r21_by_power.csv).

The existing stock captures do not contain all requested gears under controlled,
identical cookware conditions. They are sufficient to prove semantics and timing,
but not to decide whether the perceived P1/P2 difference is caused by cookware,
power-board modulation, or the custom command sequence.

## Other power-board registers

The table separates confirmed stock behavior from physical meaning. A value being
read or displayed does not mean that normal cooking uses it for control.

| Register | Observed/stock behavior | What remains unknown |
|---:|---|---|
| `R20` | Main status/fault classifier. Known codes drive debounced stock paths; `2B` is a relay transition and `29/2A` are service events. | Physical names of service events and the complete value space for other board revisions. |
| `R21` | Stored as raw telemetry, service label `I`, diagnostic range `1...60`; rises with load. | Exact quantity, units, scaling, and topology/cookware transfer function. |
| `R22` | Service label `V`; stock display adds 50 to the raw byte. | Calibration accuracy and behavior on other mains/board revisions. |
| `R23` | IGBT NTC raw; stock selects conversion family from `R28`. | Nothing material for this board after LUT recovery; other `R28` families still require evidence. |
| `R24` | Bottom NTC raw; stock selects conversion family from `R28`. | Same cross-revision limitation as `R23`. |
| `R25` | Startup capability/version byte; low nibble expected as `0xA` in a stock service path; observed `0A` during normal stock operation. | Exact bit-field definition. |
| `R26` | Output/cookware capability: `00` inactive/no pan, `01` restricted cookware, `02` unrestricted cookware. | Formal manufacturer naming; practical control semantics are established. |
| `R27` | Stored and shown as service label `B`; observed `00/01/02`. No normal cooking branch consumes it. | Physical meaning. It changes later than `R26` and is not immediate relay/topology acknowledgement. |
| `R28` | Startup board type/revision selector; values greater than 2 select newer NTC LUTs. Observed `0B`. | Exact model/revision encoding and unobserved conversion families. |
| `R29` | Startup companion capability; nonzero enables a stock cross-channel temperature-difference check. Observed `32`. | Exact bit-field definition and thresholds/intent of every bit. |
| `R2A`, `R2B` | Read at startup when `R28>=2`; used as gear parameters in two factory/service modes. Observed `1C/A4`. | Physical factory-test meaning and units. |
| `R2C`-`R2F` | Read only on the stock service page and displayed as `C`, `D`, `E`, `F`. Not present in normal passive captures. | All physical semantics and normal ranges. |

The stock startup helper `FUN_40153b7c` reads `R25`, retries the `R28/R29` pair up
to five times, reads `R24`, and, when `R28>=2`, retries the `R2A/R2B` pair up to
five times. `R2C...R2F` are not part of normal startup or the normal cooking cycle.

## Controlled custom-firmware experiment

The special build flavor `MCL02M_POWER_SWEEP_BUILD` is deliberately separate from
public production and private-sound builds. It uses the current production cooking
engine and safety logic, but gives a host runner exclusive, supervised control of
this fixed sequence:

```text
0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 25, 30,
35, 36, 40, 45, 50, 55, 56, 60, 70, 80, 90, 99
```

Each point is recorded for exactly 15 seconds **after** the commanded output has
settled. P0 is the production active-zero state, not a full Stop. Unrestricted
cookware has 6 minutes 15 seconds of dwell and normally takes roughly 7-8 minutes
including transitions. Restricted cookware stops after P35: 3 minutes 30 seconds of
dwell plus startup and transitions.

Every 500 ms record contains:

- target, applied and transmitted power;
- power-board state and selected topology;
- complete transmitted `W0D/W00/W0C` tuple;
- read/write attempt and error masks;
- validity mask and last valid `R20...R2F` values;
- converted IGBT and bottom temperatures;
- I2C error counters, cookware limitation, and fault string.

The normal live set `R20...R24`, `R26`, and `R27` remains sampled every 500 ms.
The eighth read slot rotates over `R25` and `R28...R2F`, observing every service
selector once per 4.5 seconds without lengthening the production 500 ms heartbeat.
`R21` therefore has approximately 30 samples in every 15-second dwell.

Safety and data-integrity rules:

- a physical Cancel/Stop, NoPan, fault, unexpected output change, or missing host
  heartbeat aborts the run and requests a verified production Stop;
- host contact is required every second and firmware aborts after five seconds of
  silence;
- a dwell begins only after state, target, applied/transmitted gear, and the actual
  command tuple agree;
- local encoder edits abort instead of silently contaminating a labelled point;
- restricted cookware is never asked for more than P35: after its complete P35
  dwell the run ends successfully as `COMPLETE_LIMITED`, and P36-P99 are skipped;
- the experiment does not bypass IGBT, bottom-temperature, I2C, Start, Stop, NoPan,
  cookware, or cooking-lease protection.

See [POWER_SWEEP_EXPERIMENT.md](../../firmware/production/POWER_SWEEP_EXPERIMENT.md)
for build and operating instructions.

## Comparison rules for the planned cookware study

For meaningful comparisons, use the same cooker position, water mass, initial water
temperature, lid state, and pre-boil procedure for every vessel. Record cookware
diameter/material and whether the board reports `R26=01` restriction. Do not compare
only arithmetic mean `R21`: retain the full 500 ms sequence and compare median,
percentiles, duty pattern, `R27`, topology, temperatures, and command-response timing.

The automated custom run will establish repeatability across cookware. A stock
firmware comparison still requires passive I2C capture (or a separately synchronized
electrical measurement), because stock firmware has no UART endpoint that exposes
these power-board registers.
