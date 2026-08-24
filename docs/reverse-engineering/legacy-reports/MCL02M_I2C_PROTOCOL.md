# MCL02M local power-board I²C protocol

Статус: пассивно подтверждено на `chunmi.ihcooker.v2`, ESP firmware `2.2.0_0016`, 2026-08-22. Активное управление собственной прошивкой ещё не выполнялось.

## Bus

```text
ESP32 I2C0
SDA GPIO13
SCL GPIO15
clock 10 kHz
7-bit address 0x2A
wire address byte 0x54 write / 0x55 read
cycle period approximately 500 ms
```

## Checksum

```text
checksum = (register + value) & 0xFF
```

Read:

```text
54 register
55 value checksum
```

Write:

```text
54 register value checksum
```

## Runtime cycle order

```text
R26, R27, R20, R21, R22, R23, R24, R25,
W0D, W00, W0C
```

Eight reads and all three writes are repeated every 500 ms.

## Control registers

```text
W0D  topology/state
  00  output stopped (Pause/Standby)
  80  no-pan search
  A1  topology A, gear 1..35
  C1  topology B, gear 36..55
  E1  both topologies, gear 56..99
  01  observed transitional state during high-to-low topology restart

W00  output permission
  00  disabled
  01  enabled; remains 01 during no-pan search

W0C  requested gear
  00..63  decimal 0..99; retained during no-pan search
```

Known safe stop sequence observed from stock firmware:

```text
W0D=00
about 50 ms later W00=00
about 4 ms later W0C=00
```

Known start sequence:

```text
W0D=topology
about 50 ms later W00=01
about 4 ms later W0C=initial gear
repeat heartbeat every 500 ms
```

Verified startup acknowledgement (stock runtime, 2026-08-23):

```text
the ESP sends the complete non-zero command immediately
the power board keeps R26=00 while it prepares relays/output
R26 changes to 02 after an observed 2.13...3.63 s window
R21 load feedback rises later
```

Full UI standby does not reset the bus or invoke a separate initialization
exchange.  The same read sequence and zero-valued control heartbeat continues at
500 ms.  On wake/Start the next heartbeat directly carries the requested
topology and gear.  Rapid topology changes sent during startup are internally
serialized by the power-board MCU; the interface ESP does not insert Stop or
relay deadtime.  Use `R26=02`, not `R27`, as the first digital indication of
active induction.  Evidence:
`diagnostics/i2c_2026-08-23_stock_startup_sequence/REPORT.md`.

## Feedback registers

```text
R20 raw status: 00 normal, 02 no pan, 2B observed transient/unknown nonfatal
R21 load/power feedback: rises with gear, about 3 in no-pan
R22 mains voltage raw: approximately voltage_V - 50 on this revision
R23 IGBT NTC raw: inverse lookup
R24 bottom NTC raw: inverse lookup
R25 constant 0A in this capture
R26 likely output/induction active: 02 heating, 00 off/no-pan/pause
R27 auxiliary/ring feedback: 00/01/02, exact meaning unknown
```

Measured-range approximations only:

```text
IGBT °C   ≈ -0.52955 * R23 + 156.425
bottom °C ≈ -0.54577 * R24 + 179.775
```

Production firmware must use the extracted lookup tables, not extrapolate these fits.

## No-pan state machine

```text
six consecutive R20=02 samples at 2 Hz
-> W0D=80 while W00=01 and W0C retains gear
wait for R20=00
-> restore W0D topology
-> automatic heating resume
```

The stock interface reached `W0D=80` 2.769 s after the first `R20=02` and restored topology 0.444 s after the first recovered `R20=00`.

## Fan

No independent fan write exists. The power-board MCU keeps the fan active through Pause and Standby and stopped it near 55 °C IGBT. Do not remove power from the power board before cooldown completes.

## Required validation before active use

- identify topology A/B physically;
- optionally capture a mains cold boot and registers `0x28…0x2F`;
- verify the NTC lookup tables and open/short codes;
- verify fan PWM/voltage/current;
- active command replay only with explicit authorization, water load, current monitoring and conservative IGBT limit.

Evidence: `diagnostics/i2c_2026-08-22_pass8_clean_full_sequence/REPORT.md`.
