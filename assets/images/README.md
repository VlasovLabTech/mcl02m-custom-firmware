# Image Assets

- `source/` contains the approved source artwork.
- `oled-64x48/` contains exact monochrome 64×48 PNG frames for the cooker OLED.
- `previews/` contains preparation/contact sheets.
- `manual/` contains publication artwork derived from the approved tiger.

`tools/image/prepare_oled_images.py` reproduces the 64×48 conversion. The
production firmware embeds packed 384-byte frames in
`firmware/production/main/oled_assets.c`.
