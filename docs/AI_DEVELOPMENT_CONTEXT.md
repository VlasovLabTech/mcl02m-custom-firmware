# MCL02M Custom Firmware — AI Development Context

This document is the authoritative technical handoff for future human or AI
development of the custom interface firmware for the Xiaomi Mijia MCL02M.
Read it before changing control logic, hardware mappings, persistence, or the
power-board driver. Do not infer authorization to flash hardware or start heat
from a request to edit or build software.

## 1. Project identity and scope

- Appliance: Xiaomi Mijia Induction Cooker 2.
- Model: `MCL02M`.
- Xiaomi device model: `chunmi.ihcooker.v2`.
- Interface controller: Espressif `ESP-WROOM-32D` (classic ESP32).
- Display: monochrome 64×48 OLED, page-major 384-byte framebuffer.
- Current custom source version: `0.2.5-dev`.
- Framework: ESP-IDF.
- Public repository language: English for technical documents; the device UI
  supports English, Russian, and Simplified Chinese.

The project replaces only the ESP32 interface application. The power MCU,
induction timing, relay sequencing, fan control, and primary hardware protection
remain on the original power board.

Intentionally excluded:

- Mi Home, MIoT, Xiaomi account/cloud protocols;
- stock cloud recipes and recipe catalogue;
- NFC integration;
- remote heat Start/Stop/Pause/setpoint control;
- power-board firmware modification;
- eFuse writes, Secure Boot, Flash Encryption, or a new partition table;
- automatic restart of cooking after reset or loss of power.

## 2. Non-negotiable safety rules

1. Boot must command `ALL OFF` before UI, networking, or optional services start.
2. Heating requires an explicit physical panel action. A web request must never
   arm, start, stop, pause, resume, or change a live setpoint.
3. Only one task owns power-board I²C. Other tasks read a copied snapshot.
4. The output command is repeated every 500 ms, including the zero command.
5. Only registers `0x0D`, `0x00`, and `0x0C` may be written. Reads are limited to
   selectors `0x20…0x2F`.
6. A nonzero gear is permitted only after explicit arm, valid critical readings,
   and accepted power-board status.
7. Six consecutive bad I²C cycles, invalid critical readings, unknown persistent
   power status, thermal guard, start timeout, or output active after Stop cause
   a Stop/fault transition.
8. The high-priority power-control task is registered with the 5-second task
   watchdog. A reset returns to boot Stop.
9. Hard run limit is 5 hours. Runtime state is never resumed after reset.
10. The interface ESP32 does not control the fan. Do not remove mains power until
    the power-board-controlled cooldown has completed.
11. Never erase NVS automatically. The custom Factory action erases only the
    custom namespace.
12. Any operation that writes flash, changes eFuse, or energizes heat requires
    separate explicit owner approval and an appropriate supervised test plan.

## 3. Programming and electrical connection

The ESP32 ROM bootloader is accessible at these test points:

| Point | Direction / use |
|---|---|
| `GND` | logic reference |
| `3V3` | 3.3 V rail/reference; never connect 5 V here |
| `EN` | active-low reset/enable |
| `GPIO0` | low during reset selects UART download mode |
| `RXD0` | ESP32 receive; connect USB-UART TX |
| `TXD0` | ESP32 transmit; connect USB-UART RX |

Use 3.3 V UART signalling. During development the separated interface board was
powered with 5 V through its stock low-voltage connector. Do not feed 5 V into
the `3V3` test point.

Safe connection policy:

- preferred: disconnect the interface board completely from the mains-connected
  power board and power it from a separate safe supply;
- alternative: use a proven galvanically isolated USB path with isolated power
  and data, commonly an isolated DC/DC plus an ADuM3160-class USB isolator;
- a normal USB-UART cable or earth-referenced instrument is not isolation.

## 4. ESP32 and flash facts

- Flash size: 16 MiB.
- Stock app slots:
  - `ota_0`: offset `0x10000`, size `0x160000`;
  - `ota_1`: offset `0x170000`, size `0x160000`.
- Custom development writes used the stock `ota_1` slot only.
- Current app image fits comfortably within that slot.
- Bootloader, stock partition table, `ota_0`, stock NVS, PHY, eFuse, and the
  power-board firmware are outside the intended write scope.
- On the examined device, `DISABLE_APP_CPU=1`; the second core is irreversibly
  disabled. Build and runtime must work in ESP32 unicore mode.
- The examined factory MAC eFuse payload had an invalid CRC. The custom firmware
  uses a RAM-only locally administered base MAC workaround; do not publish or
  hard-code a unit-specific MAC.
- Secure Boot and Flash Encryption were not enabled on the examined unit, and
  UART download was not eFuse-disabled. Treat those as unit-specific facts and
  verify read-only before relying on them on another cooker.

## 5. Confirmed GPIO and panel map

### Power-board I²C0

| Signal | GPIO | Notes |
|---|---:|---|
| SDA | 13 | 10 kHz, address `0x2A` |
| SCL | 15 | dedicated power-board bus |

### OLED SPI

| Signal | GPIO | Notes |
|---|---:|---|
| CS | 0 | working hardware-CS configuration; shared with boot strap |
| SCLK | 25 | SPI host 1, 2 MHz |
| MOSI | 27 | display data |
| RESET | 26 | high-low-high reset sequence |
| D/C | 33 | command/data select |

### User inputs

| Input | GPIO / protocol | Notes |
|---|---|---|
| Main encoder press `S1` | GPIO34 | active-low, external pull-up |
| Encoder `KA/KB` | GPIO5 / GPIO14 | quadrature; software orientation is calibrated clockwise-positive |
| Side touch buttons | I²C1 GPIO19/GPIO18, address `0x60`, 10 kHz | left/right events; stock code also contained a UART fallback |

Current firmware mapping is: right-side Cancel event, left-side Timer event.
Both side inputs together are treated as the Cancel path.

### Outputs

| Output | GPIO / interface | Notes |
|---|---|---|
| Buzzer | GPIO23 | LEDC/PWM note player |
| Panel LED STB | GPIO17 | three-wire serial LED driver |
| Panel LED CLK | GPIO16 | three-wire serial LED driver |
| Panel LED SDN | GPIO4 | LSB-first serial data |
| Timer/direct UI output | GPIO22 | confirmed software-controlled output |
| Unknown direct output | GPIO32 | no visible effect confirmed; firmware keeps it LOW |

The LED serial protocol resembles a TM16xx-family fixed-address driver:
commands `0x00`, `0x44`, RAM addresses `0xC0/0xC2/0xC4`, display control `0x8F`.
It drives nine white power-level LEDs and the lower blue/orange status pair.

Panel connector labels:

```text
Connector 1: STB, CLK, SDN, 5V
Connector 2: GND, S1, KA, KB
```

The OLED/touch flex carries the remaining display, touch, power, and direct-output
signals. GPIO32's physical destination is still unknown and is not required.

## 6. Power-board I²C protocol

### Bus framing

```text
7-bit address: 0x2A
wire bytes:    0x54 write / 0x55 read
clock:         10 kHz
period:        approximately 500 ms
checksum:      (register + value) & 0xFF
```

Read transaction:

```text
WRITE: register
READ:  value, checksum
```

Write transaction:

```text
WRITE: register, value, checksum
```

Normal heartbeat order:

```text
R26, R27, R20, R21, R22, R23, R24, R25,
W0D, W00, W0C
```

### Command registers

| Register | Meaning | Known values |
|---|---|---|
| `W0D` | topology/state | `00` Stop, `80` pan-search, `81` stock-derived active-zero candidate, `A1` gear 1–35, `C1` gear 36–55, `E1` gear 56–99; `01` observed transitional state |
| `W00` | output permission | `00` disabled, `01` enabled |
| `W0C` | requested gear | hexadecimal `00…63`, decimal 0…99 |

Observed stock Stop order:

```text
W0D=00
~50 ms later W00=00
~4 ms later W0C=00
```

Observed Start order:

```text
W0D=<A1/C1/E1>
~50 ms later W00=01
~4 ms later W0C=<gear>
repeat complete command every 500 ms
```

The interface does not create relay deadtime. It sends a complete nonzero command
immediately and continues the heartbeat. The power MCU disables the IGBT,
serializes relay changes, and acknowledges active induction with `R26=02` after
an observed 2.13–3.63 s startup window. Direct A↔E transitions are allowed. Do
not add artificial Stop pulses between gear ranges unless new hardware evidence
requires it.

Custom `0.2.5-dev` repeats `W0D=81, W00=00, W0C=00` for POWER gear 0,
temperature coast, and manual Pause. This is distinct from full Stop and is based
on stock static analysis; relay retention still requires supervised hardware
confirmation. UART telemetry logs every write and transition, while authenticated
Wi-Fi status exposes the state, last triple, counters, and Pause time remaining.
`MCL02M_ACTIVE_ZERO_ENABLED` and `MCL02M_ACTIVE_ZERO_DIAGNOSTICS` can disable the
behavior/extra events at compile time.

### Feedback registers

| Register | Meaning / current interpretation |
|---|---|
| `R20` | status: `00` normal, `02` no pan, `2B` transient relay state accepted for up to 10 s; stable error groups map to E-codes |
| `R21` | load/power feedback; rises with applied power |
| `R22` | mains raw; approximately `voltage V - 50` on this revision |
| `R23` | IGBT NTC raw; use extracted lookup table |
| `R24` | lower/cooking-surface NTC raw; use extracted lookup table |
| `R25` | observed constant `0A`; unknown/reserved |
| `R26` | output state: `02` induction active, `00` off/no-pan/pause |
| `R27` | auxiliary/topology feedback `00/01/02`; exact physical meaning unconfirmed |

Do not replace the lookup tables with linear fits in production. Linear fits were
useful only for early diagnostics.

### No-pan handling

Custom behavior:

1. Require three consecutive `R20=02` observations at 500 ms cadence.
2. Send `W0D=80`, keep `W00=01`, and retain the requested `W0C` gear.
3. Freeze the cooking timer, show the NoPan image, blink orange, and repeat the
   mandatory NoPan melody followed by a full 3-second silent pause.
4. If `R20=00` returns within 60 seconds, restore the correct topology and resume.
5. Otherwise Stop and latch `E02`.

NoPan and critical sounds ignore the normal Sound OFF setting.

### Error mapping

| UI code | Meaning |
|---|---|
| `E02` | no pan timeout |
| `E03` | high mains voltage group |
| `E04` | low mains voltage group |
| `E05` | lower sensor overheat |
| `E07` | IGBT overheat |
| `E08` | sensor fault |
| `E09` | interface/power-board communication fault |
| `E10` | wire/channel group |
| `E12` | power-status group |
| `EPB` | other persistent/unknown power-board status |

## 7. Firmware architecture

### Production application

`firmware/production/main/` contains:

- `app_main.c` — ordered boot: telemetry, outputs/all-off, settings, sound,
  inputs, cooking engine, display/UI, network/web, indicators;
- `cooking_engine.c` — authoritative cooking state machine, timer, delayed start,
  profiles, NoPan/fault handling, and power task;
- `temperature_ctrl.c` — PREHEAT/APPROACH/HOLD regulator;
- `ui_controller.c` — physical input events and menu state;
- `display_prod.c` — 64×48 renderer, overlays, sleep clock, images;
- `indicators.c` — white power LEDs, timer LED, blue/orange status;
- `sound.c` and `melody_tables.h` — interruptible PWM queue and approved melodies;
- `settings.c` — versioned NVS settings, profiles, Wi-Fi credentials, admin hash;
- `network_prod.c` — Wi-Fi OFF/STA/setup AP, timezone and SNTP;
- `web_server_prod.c` — local web UI, login/session/CSRF, settings and profiles;
- `oled_assets.c` — compiled 384-byte one-bit OLED frames.

Shared low-level drivers intentionally live in lab projects:

- `firmware/lab/power-test/main/powerboard_control.c`;
- `firmware/lab/ui-test/main/ui_inputs.c`;
- `firmware/lab/ui-test/main/ui_outputs.c`;
- `firmware/lab/ui-test/main/telemetry.c`.

Production CMake includes those sources directly. Moving them requires updating
`firmware/production/main/CMakeLists.txt`.

## 8. Cooking state machine and modes

States:

```text
SLEEP, IDLE, READY, DELAYED, STARTING, COOKING,
PAUSED, NO_PAN, COMPLETE, FAULT
```

### POWER

- Gear range: `0…99`; default selection is 1.
- Slow encoder movement changes by 1; fast movement changes by 5.
- Turning the encoder never starts heat.
- Gear ranges select topology automatically: 1–35 A1, 36–55 C1, 56–99 E1.
- Gear 0 enters active zero. Returning to a positive gear does not deliberately
  send a full Stop/re-arm sequence.

### TEMPERATURE

- Setpoint range: 40–190 °C, 1 °C steps.
- PREHEAT: gear 99 for error ≥45 °C, 77 for error ≥30 °C, otherwise 56;
  transition to APPROACH at 20 °C below the target.
- APPROACH: `4 + error`, capped at gear 35, so stored heat is handled earlier.
- HOLD: conservative PI capped at gear 35; base 2.0, proportional 1.25 and
  integral 0.04 per second.
- At one degree below the target or warmer, output becomes a real zero-power
  coast. Heating resumes only after cooling to three degrees below the target.
  Starting TEMPERATURE while already above its target enters the same coast
  state instead of rejecting Start.
- If gear 35 remains saturated for 90 seconds while error is at least 3 °C,
  set `HOLD SATURATED`, show an orange warning, but do not raise the cap.
- Production has a separate interface emergency cutoff at 210 °C, above the
  maximum 190 °C setpoint. The stock power MCU's native E05 remains active.

### Cooking timer

- Editable as `MM:SS`, then hours; maximum 5 hours.
- Last value is retained in RAM, not flash.
- Timer freezes in Pause and NoPan.
- Manual Pause uses active zero and performs a full Stop after two continuous hours.
- Completion stops output, plays the completion melody, shows the Ready image,
  and waits for acknowledgement.
- Timer is unavailable for an active profile because every profile stage already
  has a mandatory duration.

### Delayed start

- `START IN`: relative delay.
- `START AT`: next occurrence of the selected 24-hour time.
- The previously selected POWER/TEMPERATURE mode and setpoint are used.
- Scheduling requires prior physical setup; it is RAM-only and is cancelled by
  Cancel or a center hold.
- Manual clock is supported when offline. Confirmed SNTP time always overrides
  manual time.

### Presets/profiles

- Five persistent profiles.
- Each profile has up to five sequential timed stages.
- Each stage is POWER or TEMPERATURE and must have a nonzero duration to run.
- A timed POWER stage may use gear 0 as an active-zero wait stage. It is not manual
  Pause and therefore does not use the two-hour Pause timeout.
- Zero-duration stages are skipped.
- Total profile duration is limited to 300 minutes.
- A double stage beep marks each transition.
- Selecting a profile only loads it. Physical center Start is still required.

## 9. Physical UI contract

Main menu:

```text
1 POWER
2 T°C
3 PRESET
4 INFO
5 DELAYED START
6 SETUP
7 CLOCK
```

Controls:

- Encoder turn: navigate or edit; clockwise increases.
- Center short: Enter/confirm/Start or Pause/Resume while cooking.
- Center hold 1.5 s: Back/Stop; from Home, enter Sleep; in Factory confirmation,
  perform the physical reset.
- Timer touch: open timer editor; if a timer is active, offer disable.
- Cancel touch: immediate Back/Stop/Cancel; does not put the cooker in Sleep.
- If the OLED is off, the first center press or encoder movement only wakes it.
  Encoder changes are suppressed for another 1.5 seconds.

Settings menu:

1. Language.
2. Sound (normal sounds only; NoPan/critical remain mandatory).
3. Show live context.
4. Show IGBT temperature.
5. Timer screen mode (Auto off or Always countdown).
6. Show clock in Sleep.
7. Idle-to-Sleep minutes.
8. Active OLED timeout (1, 2, 3, 5, 10, 20, 30 min; 1–5 h).
9. Timezone (`UTC-12:00…UTC+14:00`).
10. Show the temporary I²C consecutive-bad-cycle counter (`0…6`).
11. Wi-Fi submenu.
12. Factory reset.

The sleep clock moves down one pixel per minute and cycles horizontal alignment
center → left → right to reduce OLED burn-in.

## 10. Display, LEDs, and sounds

- OLED canvas: exactly 64×48 pixels; no implicit margins.
- Small text is 5×7; the Simplified Chinese subset uses compact 8×8 glyphs.
- Avoid scrolling text. All localized static strings must pass 64-pixel width and
  glyph-coverage tests.
- Never use more than five small text lines; keep at least 2 pixels between text
  rows when composing new screens.
- Full-screen art is monochrome and exactly 64×48 (`384` packed bytes).
- The optional I²C debug counter is drawn as one small digit at `x=0, y=10`
  over the current frame. It deliberately may overlap normal content and also
  remains visible on the E09 fault picture.
- The error artwork's lower-right source label and separator are cleared during
  generation. The actual three-character fault code is rendered at 2× scale in
  the reserved `x=30…63`, `y=30…47` corner.

Approved image states:

```text
confirm, wakeup, turnon, ready, cooking, error, nopan, cancel, sleep-warning, sleep
```

Approved sounds:

```text
boot, wake, complete, no-pan, critical, sleep
```

Normal melody duty is 50%; sleep is approximately 18%. Existing click, stage,
and warning sounds remain. NoPan loops as melody + 3 s silence until recovery or
Stop. Critical uses three bursts; each burst contains two 2.5-second motifs with
4-second pauses between bursts. Acknowledgement calls `sound_stop()` immediately.

## 11. Persistence and network policy

Custom namespace: `mcl02m_v1`.

Persisted only after explicit user actions:

- versioned settings with CRC;
- five profiles;
- Wi-Fi SSID/password entered by the user;
- salted admin password hash.

Not persisted:

- active state or applied gear;
- cooking timer and delayed start;
- current clock if no SNTP source exists;
- telemetry;
- heat authorization.

Defaults:

- language English;
- normal sounds ON;
- live context ON;
- IGBT overlay OFF;
- timer screen Auto;
- Sleep after 1 minute idle;
- OLED active timeout 3 minutes;
- timezone UTC+3;
- Sleep clock ON;
- Wi-Fi OFF.

Wi-Fi behavior:

- enabled only from the physical `Setup → Wi-Fi → Power` submenu;
- if enabled with stored credentials, reconnects after reboot;
- otherwise exposes `MCL02M-SETUP-…`, password `12345678`, at
  `http://192.168.4.1/` for owner provisioning;
- credentials are never compiled into source;
- NTP servers: `pool.ntp.org`, `time.google.com`;
- local web authentication uses a random salt and 20,000 SHA-256 rounds;
- session/CSRF protection, 12-hour session, increasing delay after login failures;
- local trusted LAN only; never port-forward it to the internet;
- web API exposes status, settings, Wi-Fi onboarding, and profile editing only.

Factory reset is physical and erases only `mcl02m_v1`; it does not erase stock
Xiaomi partitions or alter eFuse.

## 12. Build, checks, and release gate

Production build:

```powershell
cd firmware\production
idf.py set-target esp32
idf.py build
python tests\safety_check.py
python tests\localization_check.py
```

Before any release or hardware write:

1. Confirm the app image is below `0x160000` bytes.
2. Validate it as an ESP32 image.
3. Run policy, safety, OLED asset, embedded web, and localization gates.
4. Review the exact write address and range.
5. Never write a merged image by default.
6. Never write bootloader, partition table, NVS, PHY, `otadata`, `ota_0`, or eFuse
   without a separately reviewed recovery plan and explicit authorization.
7. Keep a verified full flash dump outside the public repository.
8. After flashing, first perform a no-heat boot/UI/I²C soak, then supervised short
   power tests with a water load.

The current offline-built `0.2.5-dev` artifact is identified in
`firmware/production/BUILD_MANIFEST.md`. This is not permission to flash.

## 13. Remaining uncertainties and optional characterization

- The selectable range is 40–190 °C. The revised controller and stock-derived
  active-zero relay/session behavior must still be characterized under supervision.
- Production retains the 80 °C interface-side IGBT guard and uses 210 °C for
  the separate interface-side bottom cutoff; the power MCU's native E05 also
  remains active.
- E09 is generated by the interface firmware, not received as a stock E-code.
  It now requires six consecutive bad 500-ms I²C cycles (about three seconds),
  logs individual read errors, and retransmits the complete Stop sequence on
  every heartbeat while the fault remains latched. Wiring, pull-ups, supply
  integrity, and EMI coupling still need physical inspection if E09 recurs.
- The nine white LEDs run a 1.5-second all-on test after boot. If that test is
  absent on hardware, investigate the LED driver, flex, supply and GPIO path.
- Long-duration soak of web/network tasks while heating.
- Exercise every stable and transient power-board error path.
- Optionally trace GPIO32 physically; it is not a functional blocker.
- Add board-revision photographs and solder/test-point photographs.

## 14. Public repository hygiene

Never commit:

- `.env` files, Wi-Fi credentials, Xiaomi account data, cloud tokens, admin hashes;
- unit MAC addresses, local IPs, SSIDs, usernames, or absolute personal paths;
- raw flash dumps, NVS/otadata/PHY partitions, live readbacks;
- logic-analyzer/oscilloscope captures containing device-specific data;
- Ghidra project databases, tool installations, build directories, or virtual
  environments.

Those materials are stored locally under ignored `_local_private/`. When adding
new documentation, describe observations generically and redact unit-specific
identifiers. Do not distribute the whole project directory as a ZIP; publish the
Git-tracked tree only.
