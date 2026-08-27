# Reference offline build manifest

Дата сборки: 2026-08-28
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.5-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `879824` bytes (`0xD6CD0`)
- SHA-256: `9d4df0f3399fa7102ff6678a5dcbe5af48576edf4fce9bf646f43f7c5d8345cd`
- ESP image validation hash: `f4abcad9112f4b738f4d5d4a0c5e13c17fb30865cead81e0ba405c97ede9ce42`
- Stock OTA slot: `0x160000` bytes; image fits with `561968` bytes free.

## Linked memory

- Flash code: 629378 bytes
- Flash data: 145772 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36428 / 180736 bytes (20.16%)
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

Стандартное сообщение ESP-IDF предлагает общую команду `idf.py flash`; для этой
платы она запрещена процедурой проекта. Сборка не является разрешением записи.
