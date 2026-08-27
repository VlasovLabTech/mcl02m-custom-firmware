# Reference offline build manifest

Дата сборки: 2026-08-28
ESP-IDF: 6.0.2
Target: ESP32, Unicore
Firmware: `0.2.4-dev`

The app header embeds compile metadata, so a clean rebuild may have a different
SHA-256 while retaining the same source, layout, size and validation gates.
Set `MCL02M_VERIFY_MANIFEST=1` only when verifying this exact reference artifact.

## App image

- File: `build/mcl02m_custom.bin`
- Size: `877472` bytes (`0xD63A0`)
- SHA-256: `208d6539a5e4d85e8ee1e79c7c421a1cabe1402ef5b965fdda7bddc27d81eb86`
- ESP image validation hash: `a769864050953971b1fc08f66fa6cb0b9fe50bd6139afb4cb63f0648dc53795d`
- Stock OTA slot: `0x160000` bytes; image fits with `564320` bytes free.

## Linked memory

- Flash code: 627838 bytes
- Flash data: 144956 bytes
- IRAM: 89047 / 131072 bytes (67.94%)
- DRAM static: 36404 / 180736 bytes (20.14%)
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
