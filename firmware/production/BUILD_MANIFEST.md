# Reference offline build manifest

Build date: 2026-08-28
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.10-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `882272` bytes (`0xD7660`)
- SHA-256: `d44f5ef1d1fd861982acb776597dc3cbdcdc3adab2c1877e5effb4269f5b83cd`
- ESP image validation hash: `8847a37ba55f4cf08ab465247ffa233e37aabe86b557d02a604c02326fdfad56`
- Stock OTA slot: `0x160000` bytes; image fits with `559520` bytes free.

## Linked memory

- Flash code: 631042 bytes
- Flash data: 146556 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36460 / 180736 bytes (20.17%)
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

## Development-unit deployment

This `0.2.10-dev` artifact has not been flashed. The development unit remains on the
previously authorized `0.2.9-dev` image in stock `ota_1`. Building and documenting
this artifact did not access a serial port or write any device partition.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
