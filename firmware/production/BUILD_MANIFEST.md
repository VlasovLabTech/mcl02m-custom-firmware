# Reference offline build manifest

Build date: 2026-08-28
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.9-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `881872` bytes (`0xD74D0`)
- SHA-256: `911dd850765094c4dc9ae50334cf2f4d9754c18762b81ad28813943d30943d39`
- ESP image validation hash: `365719e357950b6c876d2e1c472470ba72d54cce4a9e8b89b4563f4b008e3386`
- Stock OTA slot: `0x160000` bytes; image fits with `559920` bytes free.

## Linked memory

- Flash code: 630646 bytes
- Flash data: 146556 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36444 / 180736 bytes (20.16%)
- RTC slow: 64 / 8192 bytes

## Offline gates

- `idf.py build`: PASS.
- `tests/policy_tests.py`: PASS.
- `tests/safety_check.py`: PASS.
- `tests/localization_check.py`: PASS; 101 used CJK glyphs, 66 static strings,
  complete glyph coverage, no moving text and no 1× string wider than 64 px.
- `tools/generate_oled_assets.py --check`: PASS, ten exact 384-byte frames.
- `esptool image-info`: valid checksum and validation hash, ESP32/DIO/40 MHz/16 MiB.
- Generated partition table: byte-identical to stock.
- Recovery dump: 16 MiB, SHA-256
  `e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e`.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
