# Reference offline build manifest

Build date: 2026-08-28
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.11-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `882656` bytes (`0xD77E0`)
- SHA-256: `bbd1300366631d644adb5959a00735c7972945a396370c5c1c3212dd3a76d6d1`
- ESP image validation hash: `d0e98d0e809317da6c3042071e6ee01bcdba83f1e1ff828959777bb03899b8b1`
- Stock OTA slot: `0x160000` bytes; image fits with `559136` bytes free.

## Linked memory

- Flash code: 631386 bytes
- Flash data: 146604 bytes
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

This exact `0.2.11-dev` artifact was written to the development unit's stock `ota_1`
slot at `0x170000` on 2026-08-28 after explicit owner authorization. Esptool verified
the written data hash. No bootloader, partition table, NVS, `otadata`, `ota_0`, PHY
or eFuse region was written.

ESP-IDF prints a generic `idf.py flash` suggestion after building. Project procedure
forbids that broad command on this cooker. A successful build is not authorization
to write the device.
