# OLED image pack V2

This directory contains the 36 images from `source/tiger small`, converted to
exact 64x48 one-bit PNG frames for the cooker OLED.

- The PNG files in this directory are the display-ready frames.
- `_previews/final-contact-sheet-8x.png` is the enlarged final contact sheet.
- `_previews/method-comparison-4x.png` compares the conversion methods.
- `code/oled_assets_v2.h` and `code/oled_assets_v2.c` contain all 36 frames in
  SSD1306 page-major format (384 bytes per image), plus a filename lookup table.

The conversion uses the same approved Otsu + 34% pixel-coverage pipeline as the
first image pack. The code pack is a one-to-one representation of the PNGs; it
does not apply the production firmware's special live-fault-code mask to
`error.png`.

Rebuild and verify:

```powershell
py -3 tools\image\prepare_oled_images.py `
  --input "assets\images\source\tiger small" `
  --output "assets\images\img_V2"

py -3 tools\image\generate_oled_asset_pack.py `
  --input "assets\images\img_V2" `
  --output "assets\images\img_V2\code"

py -3 tools\image\generate_oled_asset_pack.py `
  --input "assets\images\img_V2" `
  --output "assets\images\img_V2\code" `
  --check
```
