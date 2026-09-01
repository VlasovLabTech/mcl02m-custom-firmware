# Reference offline build manifest

Build date: 2026-09-01
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.34-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `909872` bytes (`0xDE230`)
- SHA-256: `dd262e2ad127a8df47e826d361879d9b82f43f2e4866b90b8017a0d4c3796c57`
- ESP image validation hash: `fd68f933ee043708202028b0c682b5ff20c23b60d03a50aaa0e880030f4a569b`
- Stock OTA slot: `0x160000` bytes; image fits with `531920` bytes free.

## Private sound flavor

- File: ignored `build_private/mcl02m_custom_private.bin`
- Size: `910112` bytes (`0xDE320`)
- SHA-256: `f874c5e02e8dc12c36df6b6585ce1d7a99ca4ba9854a4bd9412fa6acd8ed5107`
- ESP image validation hash: `0493afb5f5b1decaafd931682c6b14b3a6ed3c814eb370ba8d7f6afdc0c4ed4d`
- Stock OTA slot: `0x160000` bytes; image fits with `531680` bytes free.
- Project/app metadata: `mcl02m_custom_private`, `0.2.34-dev-private`.

## Linked memory

- Flash code: 648374 bytes
- Flash data: 156828 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 37388 / 180736 bytes (20.69%)
- RTC slow: 64 / 8192 bytes

## Offline gates

- `idf.py build`: PASS.
- `tests/policy_tests.py`: PASS.
- The policy model covers fresh/delayed/missing/late `R26=01/02`, transition and
  warning `R20`, NoPan, every known fault value, I2C gaps, the exact eight-second
  boundary, late-ack rejection, immutable EST evidence after Stop feedback, and
  every transactional Stop origin with partial writes, timeout, I2C loss, repeated
  input and late `R26=00` recovery. It also covers lease renewal in every live state,
  non-live exclusion, cooking/UI/power task suspension, expiry, normal cancellation
  and rejection of a stale generation. It additionally covers generation-tagged
  Start, active-zero, Pause and Resume confirmation, exact deadline boundaries,
  idempotent repeated requests, conflicting-request rejection, command-topology
  matching, small-cookware inference, Start-target replacement and stale-generation
  exclusion after that replacement. The package-6 cases additionally require a
  recognized pan-present status, active-zero safe hold, fresh recomputation and a
  second Resume generation; they reject unknown-return evidence and late feedback
  after Stop or Pause.
  Package 7 additionally covers independent retained-wall/heating/active-zero/
  profile-zero/Pause/NoPan clocks, frozen countdowns, five-hour profile waits under
  the eight-hour retained-session guard, every delayed-expiry menu, exact-boundary
  cancellation, long-center priority, skipped cells and RAM-only schedule loss.
  The `0.2.23-dev` cases additionally prove that best-effort startup service reads
  cannot fail an otherwise complete required probe and that profile completion
  waits for an existing output transaction before advancing.
  The `0.2.24-dev` cases additionally prove that Delayed Start refreshes the
  pre-expiry stopped power-board snapshot before feedback classification, that a
  pending Start cannot be rejected by stopped transitional feedback, and that the
  waiting screen orders mode, selected value, delay label, then countdown.
  The `0.2.25-dev` cases additionally prove that physical input dismisses an old
  timed picture before the same action may create a new one, transactional Stop uses
  the clean live-focus renderer, and `START AT` cannot enter or complete while the
  wall clock is invalid and instead shows localized `TIME NOT SET` feedback.
  The `0.2.26-dev` cases additionally prove three-frame Ready rotation, persistent
  delayed-start artwork with compact countdown and mode badge, three-second localized
  small-cookware artwork, Wi-Fi connection artwork, the exact five-second/2-on/1-off
  Hot policy, Ready priority over Hot, and Sleep rejection above a valid 60 °C reading.
  The `0.2.27-dev` case additionally proves that the small-cookware overlay uses
  `P<36` without an exclamation mark and that the OLED font contains `<`.
  The `0.2.28-dev` cases additionally parse the complete Nutcracker table and prove
  its exact 128000-ms duration, completion-driven E02 with separate 30000-ms start
  and 132000-ms post-start watchdogs,
  selective NoPan cancellation/restart, protected long-melody scheduling, and the
  fail-closed separation between the public and ignored private sound flavors.
  The `0.2.29-dev` gate additionally checks the remaining Chinese firmware/version,
  power-board, warning and Stop translations, Russian compact-width safety, complete
  CJK coverage, and all ten sections of each EN/RU/ZH user-manual panel against the
  current temperature, sound, warning, timer and fault behavior. It also proves that
  the Ready completion state remains visible through 59,999 ms and returns to Idle at
  60,000 ms so Hot, OLED timeout, and Sleep policy cannot remain blocked forever. It
  also proves that short center, long center, and Cancel during NoPan all converge on
  one idempotent transactional Stop, immediately cancel the warning melody, and that
  neither the cooking engine nor the lower power-board driver can arm a NoPan Pause
  transition capable of timing out as `EPB`.
  The `0.2.30-dev` cases additionally prove that service-only `R21/R25/R27`
  failures cannot enter E09 recovery, three critical-bad cycles enter the 320-ms
  critical-only schedule, two complete good cycles restore normal polling, transient
  outages recover, continuous critical loss faults at five seconds, continuous
  command-write loss faults at three seconds, Stop preserves its origin, and the
  first E09 RAM incident remains immutable after later feedback.
  The `0.2.31-dev` cases additionally prove that native `R20=17` E07 requires
  two consecutive matching samples, the former custom 80 °C production cutoff
  is compiled out, invalid IGBT raw data remains E08, and two valid readings
  above 92 °C create only a dismissible audible advisory with an 88 °C rearm
  threshold and no Stop or power reduction.
  The `0.2.32-dev` cases additionally prove that the advisory remains active at
  exactly 92 °C, clears below 92 °C or on Stop, repeats three beeps every three
  seconds and snoozes only its screen for seven seconds after physical input.
  They distinguish marked interface E07 after two readings above 98 °C from plain
  native E07, block Start above 80 °C, require two invalid raw sensor samples and
  six bottom readings strictly above 210 °C, and give Delayed Start at most two
  attempts including a retry after a confirmed lower-board Start timeout.
  The `0.2.33-dev` cases additionally prove every cold-Start boundary from 1 to
  99: targets 1-10 start directly, 11-35 ramp from 10, 36 starts directly,
  37-55 ramp from 36, 56 starts directly, and 57-99 ramp from 56. The exhaustive
  model also proves that a cold ramp never crosses a relay-topology boundary.
  The `0.2.34-dev` gate additionally proves that the mandatory IGBT advisory uses
  exactly three 4 kHz tones of 300 ms with two 100 ms gaps at the normal 50% duty.
  The trilingual manual gate requires the same timing plus the cross-mode
  small-cookware cap and the distinct bottom-NTC/IGBT threshold table.
- `tests/safety_check.py`: PASS.
- The public ELF contains no private LCE/SNM table or adapter symbols. The private
  flavor was built separately from the ignored local source and exposes the distinct
  `mcl02m_custom_private` project name and `0.2.34-dev-private` app version.
- Production ELF check: temporary `I2C ERRORS` menu/overlay code is absent while
  its guarded source remains available.
- `tests/localization_check.py`: PASS; 112 used CJK glyphs, 76 Chinese strings,
  81 Russian strings, complete glyph coverage, trilingual manual coverage, no
  moving text and no string wider than the applicable 64-pixel rendering path.
- `tools/generate_oled_assets.py --check`: PASS, 19 exact 384-byte frames.
- `esptool image-info`: valid checksum and validation hash, ESP32/DIO/40 MHz/16 MiB.
- Generated partition table: byte-identical to stock.
- `tools/monitor_uart.py --list`: PASS; pyserial enumerates the available ports and
  the monitor remains passive/read-only.
- Recovery dump: 16 MiB, SHA-256
  `e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e`.

## Development-unit deployment

The public `0.2.34-dev` artifact has not been flashed. The exact hash-verified
`0.2.34-dev-private` artifact (910112 bytes; SHA-256
`f874c5e02e8dc12c36df6b6585ce1d7a99ca4ba9854a4bd9412fa6acd8ed5107`)
was written only to stock `ota_1` at `0x170000` on the development unit on
2026-09-01 after explicit owner authorization. Esptool verified the written data.
The operation did not write the bootloader, partition table, `otadata`, NVS, PHY,
`ota_0` or eFuse.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
