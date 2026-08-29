# Reference offline build manifest

Build date: 2026-08-29
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.13-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `883920` bytes (`0xD7CD0`)
- SHA-256: `6e2bb28839c60cd7a9dec055b11b75523ccd36f73a75b6fca9cddf0946ea20cc`
- ESP image validation hash: `b1cd478953881c4699d98dddcbad653d14f48af170364aee85744766df79c942`
- Stock OTA slot: `0x160000` bytes; image fits with `557872` bytes free.

## Linked memory

- Flash code: 632298 bytes
- Flash data: 146956 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36484 / 180736 bytes (20.19%)
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

This `0.2.13-dev` artifact has not been flashed. The development unit remains on the
hash-verified `0.2.11-dev` image in stock `ota_1`. Building and documenting this
artifact did not write any device partition.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
