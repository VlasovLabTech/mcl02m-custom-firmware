# Reference offline build manifest

Build date: 2026-08-29
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.14-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `885184` bytes (`0xD81C0`)
- SHA-256: `4f42bb2d1477d97419bdea5392f02a2528c34ad8afc28e5ccd7e04276a5b24d5`
- ESP image validation hash: `003997088c6ff2117f677b34735e6c050dfa9f6d75188396307d7fa0d2a907aa`
- Stock OTA slot: `0x160000` bytes; image fits with `556608` bytes free.

## Linked memory

- Flash code: 633190 bytes
- Flash data: 147324 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36508 / 180736 bytes (20.20%)
- RTC slow: 64 / 8192 bytes

## Offline gates

- `idf.py build`: PASS.
- `tests/policy_tests.py`: PASS.
- `tests/safety_check.py`: PASS.
- `tests/localization_check.py`: PASS; 101 used CJK glyphs, 68 static strings,
  complete glyph coverage, no moving text and no 1× string wider than 64 px.
- `tools/generate_oled_assets.py --check`: PASS, ten exact 384-byte frames.
- `esptool image-info`: valid checksum and validation hash, ESP32/DIO/40 MHz/16 MiB.
- Generated partition table: byte-identical to stock.
- Recovery dump: 16 MiB, SHA-256
  `e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e`.

## Development-unit deployment

This `0.2.14-dev` artifact has not been flashed. The development unit remains on the
hash-verified `0.2.11-dev` image in stock `ota_1`. Building and documenting this
artifact did not write any device partition.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
