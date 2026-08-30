# Production 64x48 OLED artwork

This directory is the tracked source of truth for the production OLED frames.
Every active or reserved PNG is exactly `64x48`, contains only black and white
pixels, and is converted without dithering to the SSD1306 page-major format.

The current pack contains 19 compiled frames:

- updated Cancel, Confirm, Cooking, Error, NoPan, Wake and two Sleep frames;
- three Ready frames selected in rotation on successive timer completions;
- Wi-Fi connected, hot-surface, delayed-start and small-cookware frames;
- the unchanged startup frame;
- reserved `noopls`, `toohot` and `whatisgoingon` frames, which are compiled but
  deliberately have no display trigger yet.

`error.png` keeps its lower-right corner available for the live 2x error code.
The delayed-start frame receives a compact countdown and `P`, `t` or `pr` mode
badge at runtime. The small-cookware frame receives `P<36` and a localized label
at runtime.

Regenerate and verify the C resources:

```powershell
py -3 firmware\production\tools\generate_oled_assets.py
py -3 firmware\production\tools\generate_oled_assets.py --check
```

The legacy `coocking.png` and `ready.png` files remain only as historical source
art and are not compiled into the current firmware.
