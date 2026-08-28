# Reference offline build manifest

Дата сборки: 2026-08-28
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.7-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `881520` bytes (`0xD7370`)
- SHA-256: `752c5f7b069a8f5fcb4c8b9f6ee25965c8ce8bd42755a82d847c16c68536ccfc`
- ESP image validation hash: `539d4a598176b67ea28e07459bfedb6547d5b87d8962644ed3f57d6cf79fc7b6`
- Stock OTA slot: `0x160000` bytes; image fits with `560272` bytes free.

## Linked memory

- Flash code: 630330 bytes
- Flash data: 146524 bytes
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
