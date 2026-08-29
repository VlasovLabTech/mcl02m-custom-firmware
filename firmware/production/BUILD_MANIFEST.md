# Reference offline build manifest

Build date: 2026-08-30
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.24-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `897856` bytes (`0xDB340`)
- SHA-256: `f547b7dd4ba9bddb1a50b5ff6a9ad9d920a579b77a55acac8e06d3c8571fd21c`
- ESP image validation hash: `95bd83e8062b880239c577f99a853bd45e56d61cc4784b2a16744b99b2daa382`
- Stock OTA slot: `0x160000` bytes; image fits with `543936` bytes free.

## Linked memory

- Flash code: 642122 bytes
- Flash data: 151068 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 37028 / 180736 bytes (20.49%)
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
- `tests/safety_check.py`: PASS.
- Production ELF check: temporary `I2C ERRORS` menu/overlay code is absent while
  its guarded source remains available.
- `tests/localization_check.py`: PASS; 101 used CJK glyphs, 68 static strings,
  complete glyph coverage, no moving text and no 1× string wider than 64 px.
- `tools/generate_oled_assets.py --check`: PASS, ten exact 384-byte frames.
- `esptool image-info`: valid checksum and validation hash, ESP32/DIO/40 MHz/16 MiB.
- Generated partition table: byte-identical to stock.
- `tools/monitor_uart.py --list`: PASS; pyserial enumerates the available ports and
  the monitor remains passive/read-only.
- Recovery dump: 16 MiB, SHA-256
  `e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e`.

## Development-unit deployment

This hash-verified `0.2.24-dev` artifact was written only to stock `ota_1` at
`0x170000` on 2026-08-30 after explicit owner authorization. Esptool verified the
written data. That operation did not write the
bootloader, partition table, `otadata`, NVS, PHY, `ota_0` or eFuse.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
