# MCL02M Power-Board Protocol

This is the compact implementation reference for the local interface between the
ESP32 panel board and the original MCL02M power board. The protocol was derived
from passive logic captures, stock-firmware analysis, synchronized oscilloscope
tests, and controlled custom-firmware tests.

## Electrical/bus settings

| Property | Value |
|---|---|
| ESP32 bus | I²C0 |
| SDA / SCL | GPIO13 / GPIO15 |
| Clock | 10 kHz |
| 7-bit address | `0x2A` |
| Wire address | `0x54` write, `0x55` read |
| Heartbeat | approximately 500 ms |
| Checksum | `(register + value) & 0xFF` |

Reads send the selector, then receive `value, checksum`. Writes send
`register, value, checksum`.

```text
R26 R27 R20 R21 R22 R23 R24 R25 W0D W00 W0C
```

This complete cycle repeats every 500 ms in active, paused, idle, and standby
states. The power driver must be the only I²C owner.

## Commands

| Register | Values |
|---|---|
| `0x0D` | `00` Stop; `80` pan search; `81` stock-derived active-zero candidate; `A1` gears 1–35; `C1` gears 36–55; `E1` gears 56–99 |
| `0x00` | `00` output disabled; `01` output requested |
| `0x0C` | requested gear, hexadecimal `00…63` = decimal 0…99 |

Stock Stop sequence:

```text
0D 00
~50 ms
00 00
~4 ms
0C 00
```

Stock Start sequence:

```text
0D <topology>
~50 ms
00 01
~4 ms
0C <gear>
```

Active-zero command used by custom firmware for POWER 0, temperature coast,
and manual Pause:

```text
0D 81
~50 ms
00 00
~4 ms
0C 00
```

This sequence is distinct from full Stop and was recovered from stock-firmware
static analysis. Supervised tests confirmed retained-session POWER 0 and
Pause/Resume without unintended relay switching. The production driver exposes
every write, the last command triple, active-zero transition counters, and state
through UART telemetry and authenticated Wi-Fi status. The diagnostics are
compile-time removable.

Do not introduce a synthetic Stop between topology changes. The ESP sends the new
nonzero command and the power MCU performs IGBT-off, relay sequencing, and
deadtime internally. Both `R26=01` and `R26=02` are valid active-output
acknowledgements; `01` additionally limits the available cookware capability.

For a target in `1…35` or `56…99`, the ramp uses ten-gear steps inside the current
topology and crosses the gap directly at `35 ↔ 56`. It does not transiently command
the C1 range merely to reach A1 or E1. A manually requested target in `36…55`
remains valid and uses C1 normally.

## Feedback

| Register | Interpretation |
|---|---|
| `0x20` | status: `00` normal, `02` no pan, `2B` short relay transition, other stable groups are faults |
| `0x21` | load/power feedback |
| `0x22` | mains raw, approximately volts minus 50 on the tested revision |
| `0x23` | IGBT NTC raw, converted by lookup table |
| `0x24` | lower NTC raw, converted by lookup table |
| `0x25` | startup capability/version byte; low nibble `0A` on the tested revision |
| `0x26` | output/cookware capability: `00` inactive, `01` restricted, `02` unrestricted |
| `0x27` | auxiliary/topology feedback `00/01/02`, exact meaning unknown |

The transient `R20=2B` is accepted for no more than 10 seconds. Use the extracted
NTC lookup tables; early linear diagnostic fits are not production conversions.

## Restricted-cookware flow

Stock-firmware analysis and a live small/large cookware comparison establish
`R26=01` as a valid restricted-cookware response, not a fault. The stock panel caps
the commanded gear at decimal 35 and forces the `A1` topology. Custom firmware
applies the same physical limit in POWER, TEMPERATURE, profiles, and retained-session
Resume. In POWER mode it also replaces the selected value with the honest permitted
value. The OLED first explains `SMALL COOKWARE / POWER LIMITED / MAX35`; subsequent
attempts to raise POWER above 35 are rejected and repeat the notice, while downward
adjustments remain available. `R26=02` clears the restriction.

## No-pan flow

After three consecutive 500 ms samples of `R20=02`, send `W0D=80` while keeping
`W00=01` and retaining `W0C`. On `R20=00`, restore the topology for the retained
gear. The custom UI gives the user 60 seconds before latching E02.

## Safety observations

- The power board retained output during a deliberate 5-second heartbeat gap;
  no sufficiently short independent communication watchdog was found.
- Therefore the ESP high-priority task watchdog and safe boot Stop are mandatory.
- The fan is entirely controlled by the power MCU. No fan register was found.
- Relay and output setup after Start took about 2.13–3.63 seconds before a valid
  `R26=01/02` acknowledgement; this is normal and must not be treated as instant Start.
- Only `0x0D`, `0x00`, and `0x0C` are writable in this project.
- Manual Pause uses the active-zero candidate but remains a distinct logical
  state. It becomes full Stop after two hours; POWER 0 and profile wait stages do not.

For full rationale and state-machine rules, see
[AI Development Context](AI_DEVELOPMENT_CONTEXT.md). The complete stock-response
re-audit is preserved in
[Stock Power-Board Response Audit](reverse-engineering/STOCK_POWER_BOARD_RESPONSE_AUDIT.md).
