# Reference offline build manifest

Build date: 2026-08-29
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.16-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `885216` bytes (`0xD81E0`)
- SHA-256: `1defea943fb10235a3d67e3ecc1c4ec0cb14b0ee82a7039e229e3e7702163846`
- ESP image validation hash: `e9176261db752d513268f19e1a42c71ce54bca1d18ac2f162ab115de709e29f7`
- Stock OTA slot: `0x160000` bytes; image fits with `556576` bytes free.

## Linked memory

- Flash code: 633254 bytes
- Flash data: 147292 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36500 / 180736 bytes (20.20%)
- RTC slow: 64 / 8192 bytes

## Offline gates

- `idf.py build`: PASS.
- `tests/policy_tests.py`: PASS.
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

This `0.2.16-dev` artifact has not been flashed. The development unit remains on the
hash-verified `0.2.14-dev` image in stock `ota_1`, written at `0x170000` on
2026-08-29. That deployment changed only the application slot. Building and
documenting `0.2.16-dev` did not write any device partition.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
