# Reference offline build manifest

Build date: 2026-08-30
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.29-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `904256` bytes (`0xDCC40`)
- SHA-256: `b9a3770f383564c6f0d95bfc169498b92b7f91e2d107c15e0ba39482de6f88f9`
- ESP image validation hash: `2e56a6d7c0756a79abc86a68984b48fc19dff3848fc81b828de3b48f61979a23`
- Stock OTA slot: `0x160000` bytes; image fits with `537536` bytes free.

## Private sound flavor

- File: ignored `build_private/mcl02m_custom_private.bin`
- Size: `904480` bytes (`0xDCD20`)
- SHA-256: `476c6940d174cdb6a6f8e6f26c93686f6e1319fde3a69599139f005696ef2de5`
- ESP image validation hash: `8bf93a73f5fae7754d122b973735db61a1267712e07dc169d64b9da5dbf49a03`
- Stock OTA slot: `0x160000` bytes; image fits with `537312` bytes free.
- Project/app metadata: `mcl02m_custom_private`, `0.2.29-dev-private`.

## Linked memory

- Flash code: 643902 bytes
- Flash data: 155676 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 37180 / 180736 bytes (20.57%)
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
- `tests/safety_check.py`: PASS.
- The public ELF contains no private LCE/SNM table or adapter symbols. The private
  flavor was built separately from the ignored local source and exposes the distinct
  `mcl02m_custom_private` project name and `0.2.29-dev-private` app version.
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

The public `0.2.29-dev` artifact has not been flashed. The exact hash-verified
`0.2.29-dev-private` artifact (904480 bytes; SHA-256
`476c6940d174cdb6a6f8e6f26c93686f6e1319fde3a69599139f005696ef2de5`)
was written only to stock `ota_1` at `0x170000` on the development unit on
2026-08-30 after explicit owner authorization. Esptool verified the written data.
That operation did not write the bootloader, partition table, `otadata`, NVS, PHY,
`ota_0` or eFuse.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
