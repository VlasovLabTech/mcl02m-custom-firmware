# Reference offline build manifest

Build date: 2026-08-29
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.17-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `886160` bytes (`0xD8590`)
- SHA-256: `4a55956e05cc41cbeda34e78a724119a38659ed74217e7e70471fbf5ccbe7296`
- ESP image validation hash: `143d81876280f01bc782a57a0a97f61c7f8f88979d982ada1391215b5e72322c`
- Stock OTA slot: `0x160000` bytes; image fits with `555632` bytes free.

## Linked memory

- Flash code: 633822 bytes
- Flash data: 147660 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36580 / 180736 bytes (20.24%)
- RTC slow: 64 / 8192 bytes

## Offline gates

- `idf.py build`: PASS.
- `tests/policy_tests.py`: PASS.
- The policy model covers fresh/delayed/missing/late `R26=01/02`, transition and
  warning `R20`, NoPan, every known fault value, I2C gaps, the exact eight-second
  boundary, late-ack rejection, and immutable EST evidence after Stop feedback.
- `tests/safety_check.py`: PASS.
- Production ELF check: temporary `I2C ERRORS` menu/overlay code is absent while
  its guarded source remains available.
- `tests/localization_check.py`: PASS; 101 used CJK glyphs, 68 static strings,
  complete glyph coverage, no moving text and no 1× string wider than 64 px.
- `tools/generate_oled_assets.py --check`: PASS, ten exact 384-byte frames.
- `esptool image-info`: valid checksum and validation hash, ESP32/DIO/40 MHz/16 MiB.
- Generated partition table: byte-identical to stock.
- Recovery dump: 16 MiB, SHA-256
  `e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e`.

## Development-unit deployment

This exact `0.2.17-dev` artifact was flashed to the development unit's stock `ota_1`
at `0x170000` on 2026-08-29 after explicit owner authorization. Esptool verified the
written data. The operation wrote only the application image and did not write the
bootloader, partition table, `otadata`, NVS, PHY, `ota_0` or eFuse.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
