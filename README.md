<div align="center">

<img src="assets/images/manual/happy-tiger-color.png"
     alt="Happy MCL02M tiger" width="320">

# Xiaomi Mijia MCL02M Custom Firmware

**English · Русский · 简体中文 · local-first · physical-control-first**

</div>

This repository contains replacement interface firmware for the **Xiaomi Mijia
Induction Cooker 2, model MCL02M** (`chunmi.ihcooker.v2`). The original appliance
was sold with a Chinese-only local interface. Its detachable control board uses
an ordinary **Espressif ESP-WROOM-32D** module, which made it possible to study
the panel, document the power-board protocol, and build a new local interface.

The custom firmware adds English, Russian, and Simplified Chinese UI, while
keeping all heating commands behind the physical controls on the cooker.

> # **NO MI HOME, MIOT, XIAOMI CLOUD, NFC OR STOCK RECIPES**
>
> **Those original functions are intentionally not supported.** The project uses
> its own local UI, local Wi-Fi provisioning, local web settings, profiles, and
> safety state machine. The web interface cannot start or stop heating.

## What the custom firmware adds

- direct **POWER** control from `0…99`;
- stock-compatible **small-cookware limiting**: restricted power-board feedback
  caps the real output at gear 35 in every mode, reports the permitted POWER value,
  and explains blocked upward adjustments on the OLED;
- **temperature control with a PI/PID-style regulator**, high-power preheat,
  conservative approach, and a hold stage capped at gear 35;
- live NTC temperature, IGBT temperature, mains voltage, selected/applied power,
  countdown, and state information on the 64×48 OLED;
- cooking timer with seconds, manual 24-hour clock, SNTP time synchronization,
  **START IN** and **START AT** delayed start;
- five persistent presets, each containing up to five timed POWER or TEMPERATURE
  stages executed in sequence;
- English, Russian, and Simplified Chinese panel UI;
- local Wi-Fi onboarding and a self-contained web page for settings and preset
  editing; credentials are entered by the owner and stored in the custom NVS
  namespace, never compiled into the firmware;
- custom OLED artwork, power/status LEDs, and six PWM melodies;
- NoPan recovery, latched errors, thermal guards, hard run limit, safe boot Stop,
  and a single 500 ms power-control heartbeat.
- dismissible warnings with the exact raw value for previously unseen nonzero
  power-board `R20` statuses; known fault groups retain their normal E-codes.

The complete three-language operating guide is available as a single offline
file: **[User manual](docs/user-manual.html)**.

> # **BACK UP THE COMPLETE 16 MiB FLASH BEFORE YOU WRITE ANYTHING**
>
> **Do not flash this project until you have read the ESP32's entire flash from
> `0x000000` through `0xFFFFFF`, saved it in at least two safe locations, and
> verified the backup by reading it twice and comparing SHA-256 hashes.**
>
> The original image contains the stock firmware as well as device-specific
> partitions and settings. Without a verified full-flash backup, restoring this
> particular cooker to its original factory state may be impossible. A copy of
> somebody else's dump is not an equivalent recovery image and must not be
> published with this repository. See [Flashing and recovery](docs/FLASHING.md)
> before connecting the programmer.

## Hardware and programming connection

The interface board exposes ROM-bootloader test points:

| Test point | Purpose |
|---|---|
| `GND` | logic ground |
| `3V3` | 3.3 V logic rail/reference — **never apply 5 V here** |
| `EN` | ESP32 reset/enable |
| `GPIO0` | hold low across reset to enter the ROM bootloader |
| `RXD0` | ESP32 UART receive; connect to USB-UART TX |
| `TXD0` | ESP32 UART transmit; connect to USB-UART RX |

The firmware was programmed with a **3.3 V logic USB-UART adapter**. To enter
download mode, pull `GPIO0` low, pulse `EN` low/high, then release `GPIO0`.
The board used during development was powered separately with **5 V through its
original low-voltage panel connector**, not through the `3V3` test point.

> # **MAINS-ISOLATION REQUIREMENT — READ BEFORE CONNECTING USB**
>
> **Program the ESP32 only when the interface board is physically disconnected
> from the cooker power board and powered from a separate safe supply, or use a
> proven galvanically isolated USB path.**
>
> Many available USB isolator modules combine an isolated DC/DC converter with
> an **ADuM3160-class USB data isolator**. Confirm that the module really isolates
> both data and power, has adequate insulation and voltage rating, and is used
> within its specified USB speed/current limits. A normal USB-UART adapter, a
> laptop charger, or an oscilloscope ground is **not** galvanic isolation.

<p align="center">
  <img src="hardware/adum3160-usb-isolator.png"
       alt="ADuM3160-based USB galvanic isolator" width="440">
</p>

<p align="center"><em>An ADuM3160-based USB galvanic isolator of the type
discussed above. Always verify the specifications and actual isolation of the
particular module before use.</em></p>

The induction cooker contains rectified mains and high-current switching nodes.
Do not work on it energized unless you are qualified and have an isolation plan.
Never connect an earth-referenced scope or non-isolated USB ground to an unknown
internal ground. Allow the power-board-controlled fan to finish cooling before
removing power.

### Accessing the programming points

> **Removing the interface board is not recommended.** The rotary encoder makes
> board removal extremely difficult. On the unit documented here, the author
> instead used a regular utility knife to cut away a small section of the plastic
> housing, working carefully and gradually, step by step. This exposed the
> programming area without removing the board. Disconnect the appliance from
> mains power before doing any mechanical work, and take care not to damage the
> PCB, wiring, or nearby components.

![Interface board with the plastic housing carefully cut away](hardware/interface-board-access.jpg)

*Overall view of the interface board after creating access to the programming
area.*

![Close-up of the ESP32 programming connections](hardware/programming-connections.jpg)

*Close-up of the soldered programming connections. Follow the test-point table
above rather than relying on wire colors.*

A link to the full project article will be added later.

## Repository map

```text
assets/
  images/                 source art, 64×48 OLED frames, manual illustration
  sounds/                 final six-melody pack and design experiments
docs/
  AI_DEVELOPMENT_CONTEXT.md
  STATE_MACHINE_IMPLEMENTATION_PLAN.md
  POWER_BOARD_PROTOCOL.md
  FLASHING.md
  user-manual.html        self-contained EN/RU/ZH operating manual
firmware/
  production/             current ESP-IDF application
  lab/power-test/         power-board test harness
  lab/ui-test/            panel test harness and shared UI drivers
hardware/                 pinout and connection photographs
tools/                    image and reverse-engineering utilities
```

Local dumps, credentials, Ghidra workspaces, raw captures, and build artifacts
are deliberately stored under the ignored `_local_private/` directory and are
not part of the public repository.

## Build

Use a compatible ESP-IDF environment:

```powershell
cd firmware\production
idf.py set-target esp32
idf.py build
python tests\policy_tests.py
python tests\safety_check.py
python tests\localization_check.py
```

The stock 16 MiB flash layout contains two 0x160000-byte app slots. Development
used only the stock `ota_1` application slot at `0x170000`; the bootloader,
partition table, `ota_0`, stock NVS, eFuse, and power-board firmware were not
modified. See [Flashing and recovery](docs/FLASHING.md) before writing anything.

## Development status

The current source version is `0.2.34-dev`; the hash-verified
`0.2.34-dev-private` app image was written only to stock `ota_1` at `0x170000`
on the development unit on 2026-09-01.
Supervised testing of the preceding `0.2.24-dev` image confirmed retained-session active zero without
unexpected relay switching, Sleep/Wake, the temporary I2C debug display, and
temperature operation with water. A 125 °C empty-pan test then showed about 5 °C of first-heat
overshoot while subsequent holding remained accurate. Source `0.2.10-dev` adds
four-second rate-adaptive braking with phase hysteresis, recomputes temperature output
before Resume, and crosses directly between low and high power topologies instead of
transiently requesting gears 36…55. Source `0.2.11-dev` also adds a physical Settings
screen for the firmware version. Versions `0.2.12-dev` through `0.2.14-dev` add five
bounded state-integrity fixes, restricted-cookware handling, a complete power-board
response/register re-audit, live `R28`, and physical-input-acknowledged unknown-`R20`
warnings. The `0.2.15-dev` source excludes the temporary I2C-error counter from
the production menu and OLED while retaining its implementation behind a disabled
build flag. It also revises the deferred implementation packages around the observed
protocol. Version `0.2.16-dev` removes the queued timer-toggle race and changes the
physical timer editor to `seconds → minutes → hours`, with an explicit confirmation
for each field. Version `0.2.17-dev` completes the Start/EST evidence package: the
eight-second acknowledgement window begins only after the first successfully
transmitted nonzero heartbeat, closes strictly at its deadline, and preserves an
immutable first-cause RAM incident in compact UART and authenticated status. Its host
model covers accepted, delayed, missing, late, fault, NoPan, warning and I²C-gap
responses. Version `0.2.18-dev` adds a transactional `STOPPING` state shared by
normal completion and every safety origin. It continues sending the complete zero
command until two fresh `R26=00` samples confirm output-off, and it preserves timeout
or I²C-loss evidence while retrying instead of reporting a false `IDLE/COMPLETE`.
Version `0.2.19-dev` adds a generation-tagged three-second cooking lease. Only the
cooking task renews it in a legitimate live session; expiry is detected by the
independent 500-ms power task and enters the same repeated Stop transaction with a
distinct `ECL / COOK LEASE` cause. The
`0.2.20-dev` source makes Start, active zero, Pause and Resume explicit
generation-tagged transactions. It separates requested, successfully transmitted,
feedback-observed and inferred-confirmed state, accepts confirmation only from a
fresh compatible `R20/R26` sample after the matching command, preserves the proven
`81/00/00` active-zero command, and reports bounded timeout or rejection reasons
without pretending that the power board reports a gear value. Version
`0.2.21-dev` makes cookware return transactional. Recognized pan-present feedback
first confirms active zero; the cooking layer then refreshes readings, resets the
interrupted temperature-control episode, applies the small-cookware cap, recomputes
output, and confirms a separate Resume generation. Unknown `R20` warnings cannot
prove return, and Stop or Pause invalidates an in-flight return generation. The
`0.2.23-dev` source separates the five-hour user/profile countdown from
an eight-hour retained-session wall guard, the two-hour manual-Pause limit, the
three-second cooking lease, and diagnostic heating/active-zero/profile-zero/NoPan
time buckets. Delayed Start now returns the physical UI to the actual POWER,
TEMPERATURE, or PROFILE view regardless of which menu was open, and long-center
Stop or delayed Cancel has priority over the timer editor. The
same `0.2.23-dev` batch makes `R2C`–`R2F` nonfatal best-effort startup diagnostics,
adds one-shot boot and cooking-event compact UART records, clears stale temperature
trend evidence after an actual reading gap, prevents profile-cell completion from
racing an in-flight output transaction, and normalizes the Pause-only diagnostic
gear lifetime. Version `0.2.24-dev` fixes the supervised-test Delayed Start race:
after expiry the engine refreshes the power-board snapshot before classifying
feedback, and a still-pending Start cannot mistake the prior stopped state for
`ETM`. Its waiting screen orders the selected value directly below `POWER` or
`T°C`, before the delay label and countdown. Version `0.2.25-dev` lets any new
physical press or encoder movement dismiss an existing timed picture immediately;
an action such as Cancel may then create its own new picture. Transactional Stop
keeps the clean large live screen instead of briefly exposing technical `STOPPING`
text. `START AT` refuses an invalid wall clock with a localized `TIME / NOT SET`
screen rather than opening or accepting a time editor. Version `0.2.26-dev`
replaces the production OLED artwork and adds three rotating completion frames.
Delayed Start uses its dedicated picture with a compact countdown plus
`P`/`t`/`pr` mode badge. Small cookware uses its dedicated three-second picture
with `P<36` and a localized label. Successful Wi-Fi joins show the Wi-Fi picture,
including when entering an already-connected Wi-Fi menu. After cooking stops, a
valid surface reading above 60 °C produces a blinking hot-surface picture after
five seconds of inactivity; timer-completion Ready keeps priority for at most one
minute before Idle resumes Hot/OLED/Sleep policy, and Sleep remains blocked until
the surface cools. Three additional
frames are compiled but deliberately have no trigger yet. Version `0.2.27-dev`
removes the exclamation mark from the cookware limit and adds the previously missing
`<` OLED glyph. Version `0.2.28-dev` replaces the NoPan loop with one complete,
mandatory 128-second Nutcracker melody and raises E02 only after playback completes.
A separate watchdog faults if playback cannot start within 30 seconds or cannot
report completion within 132 seconds after it starts. Returning cookware, Pause, or Stop cancels only
that NoPan request, so removing cookware again restarts the melody from its beginning.
While NoPan is active, center short, center long, and Cancel all perform the same
idempotent transactional Stop and cancel the warning melody immediately; the
NoPan path cannot enter Pause.
Long transition melodies discard ordinary queued clicks. Public and opt-in ignored
private builds share every source except the selected Wake/Sleep tables; their project
names and build directories are distinct. Version `0.2.29-dev` completed the
English/Russian/Simplified-Chinese OLED audit, localizes the remaining Chinese
version/status labels and the unknown-`R20` warning, and updates the self-contained
trilingual user manual for the current control, safety, artwork and sound behavior.
Version `0.2.30-dev` replaced the coarse cycle-count E09 trigger with classified,
time-based recovery: service-only `R21/R25/R27` failures cannot trip E09; three
critical-bad cycles enter a 320-ms critical-only poll; two good critical cycles leave
recovery; continuous critical loss faults after five seconds and continuous command
write loss after three seconds. The first-cause masks, timers, state and command
snapshot remain in RAM and authenticated diagnostics.
Current `0.2.34-dev` keeps native E07 debounced at two matching `R20=17` samples.
During an active session, two valid readings above 92 °C start a persistent
`IGBT / >92°C` warning with three 4 kHz, 300 ms beeps separated by 100 ms every
three seconds; physical input hides
only the screen for seven seconds and the warning clears strictly below 92 °C.
Two valid readings above 98 °C produce a separately marked interface E07, and Start
is blocked above 80 °C. Raw sensor faults require two samples and the bottom emergency
ceiling requires six consecutive samples strictly above 210 °C. Delayed Start now
retries one immediate rejection or confirmed Start timeout once. The complete inventory of custom runtime
limits is maintained in the
[production limits and automatic-stop audit](docs/PRODUCTION_LIMITS_AND_AUTOMATIC_STOPS.md).
Cold Start now selects the target's final relay topology immediately and ramps only
inside it: `1…10` direct, `11…35` from 10, `36` direct, `37…55` from 36, `56`
direct, and `57…99` from 56. This preserves a gentle ramp without artificial
intermediate relay/IGBT transitions.
The supervised
operator/monitor sequence is documented in the
[hardware validation plan](docs/HARDWARE_VALIDATION_PLAN.md). The
remaining findings and proposed fix sequence are preserved in the
[state-machine implementation plan](docs/STATE_MACHINE_IMPLEMENTATION_PLAN.md).
The `0.2.34-dev-private` app (SHA-256
`f874c5e02e8dc12c36df6b6585ce1d7a99ca4ba9854a4bd9412fa6acd8ed5107`) was
flashed after explicit owner authorization. Esptool verified the written data and
hard-reset the ESP32; no bootloader, partition table, `otadata`, NVS or other
partition was written.

This is an independent community project, not an official Xiaomi or Chunmi
product. Use it at your own risk. Licensed under the [MIT License](LICENSE).
