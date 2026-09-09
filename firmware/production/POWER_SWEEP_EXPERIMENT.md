# Supervised cookware power-sweep firmware

This is an experiment-only firmware flavor for comparing power-board feedback across
cookware. It is not a production release and it must not be left installed after the
measurement campaign.

## What it does

After a host command, firmware starts the normal production POWER mode and visits:

```text
P0, P1, P2, P3, P4, P5, P6, P7, P10, P15, P20, P25, P30,
P35, P36, P40, P45, P50, P55, P56, P60, P70, P80, P90, P99
```

It waits for every transition to settle, records that point for 15 seconds, and then
moves to the next point. P0 is active zero. An unrestricted run has 375 seconds of
dwell plus startup and transition time, normally about 7-8 minutes. A restricted
run ends after P35 and has 210 seconds of dwell plus transitions.

The build preserves the production command path and protections. For restricted
cookware (`R26=01`) it records through P35 and then ends successfully with
`COMPLETE_LIMITED`. It never requests P36-P99 for that vessel.

## Build

Use the ESP-IDF 6.0.2 shell configured for this project:

```powershell
cd C:\Users\User\Projects\Xiaomi-Mijia-MCL02M\firmware\production
idf.py -B build_sweep -D MCL02M_POWER_SWEEP_BUILD=ON build
```

The resulting application is:

```text
build_sweep/mcl02m_power_sweep.bin
```

Its project/version identity is `mcl02m_power_sweep / 0.2.38-dev-sweep`. The flavor
is mutually exclusive with `MCL02M_PRIVATE_SOUND_BUILD`.

Do not flash or erase bootloader, partition table, NVS, PHY, or factory data for this
test. On the validated cooker, only the application image in the selected OTA slot
is replaced, using the same app-only procedure as the normal private build.

## Prepare one cookware run

1. Put the selected vessel in the same position used for every comparison.
2. Use the agreed identical water mass, starting temperature, and lid condition.
3. If the protocol requires pre-boiled water, stabilize it consistently before
   starting the automated run.
4. Connect the short UART cable and identify its COM port.
5. Do not touch the encoder or buttons during the run except Cancel for an emergency.

Use a short ASCII label such as `frybest_large`, `frybest_small`, or `katyusha`:

```powershell
cd C:\Users\User\Projects\Xiaomi-Mijia-MCL02M
python tools\power_sweep_runner.py COM5 --pan frybest_large
```

The runner performs the handshake, starts the sequence, sends a one-second host
heartbeat, prints each transition, and saves all files under the gitignored path:

```text
_local_private/validation/power-sweeps/<timestamp>-<pan>/
```

Outputs:

- `raw_uart.log`: complete received serial stream;
- `samples.csv`: one normalized row per 500 ms telemetry frame;
- `summary.csv`: per-power `R21` statistics and relevant state ranges;
- `summary.json`: the same summary plus final run result.

## Abort behavior

Pressing physical Cancel/Stop, removing cookware, producing any production fault,
changing the output locally, pressing Ctrl+C in the runner, disconnecting UART, or
losing host heartbeat aborts the run. Firmware then requests Stop through the normal
transactional production path. The power task continues its own Stop retransmission
policy even if the host is gone.

After an abort, inspect the final `X,ABORTED,<pan>,<reason>` frame and do not reuse an
incomplete summary as a full cookware run.

## Compact protocol

Host commands:

```text
X,PING
X,START,<label>
X,ABORT
```

Important firmware frames:

```text
X,READY,<version>,25,15000
X,P,<requested>,SET
X,P,<requested>,BEGIN,<actual>,<limited>
X,P,<requested>,END,<actual>,<limited>
X,D,...
X,DONE,<label>,COMPLETE
X,ABORTED,<label>,<reason>
```

`X,D` retains full diagnostic coverage in a compact CSV frame. It contains every
last-valid `R20...R2F` byte plus per-cycle masks showing exactly which selectors were
attempted, valid, or failed. This avoids verbose JSON on the short-lived UART link.

## Return to normal firmware

After all cookware runs, rebuild/flash the desired normal public or private-sound
application with its ordinary app-only procedure. The sweep flavor does not change
NVS schema or persist experiment state.
