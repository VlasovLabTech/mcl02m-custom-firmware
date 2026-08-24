#!/usr/bin/env python3
"""Offline Chinese localization/font coverage and static-layout gate."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
OUTPUTS = ROOT.parents[1] / "firmware" / "lab" / "ui-test" / "main" / "ui_outputs.c"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def c_strings(source: str) -> list[str]:
    return re.findall(r'"((?:\\.|[^"\\])*)"', source)


def is_cjk(character: str) -> bool:
    return 0x3400 <= ord(character) <= 0x9FFF


def static_width(text: str) -> int:
    """Mirror the regular 1x OLED layout: CJK=8px, other glyphs=5px, gap=1px."""
    if not text:
        return 0
    return sum(8 if is_cjk(char) else 5 for char in text) + len(text) - 1


def main() -> int:
    ui = (MAIN / "ui_controller.c").read_text(encoding="utf-8")
    display = (MAIN / "display_prod.c").read_text(encoding="utf-8")
    types = (MAIN / "app_types.h").read_text(encoding="utf-8")
    settings = (MAIN / "settings.c").read_text(encoding="utf-8")
    web = (MAIN / "web_server_prod.c").read_text(encoding="utf-8")
    outputs = OUTPUTS.read_text(encoding="utf-8")
    localized_source = ui + "\n" + display

    if "LANG_ZH = 2" not in types or "settings->language > LANG_ZH" not in settings:
        fail("Simplified Chinese is not a persisted third language")
    if '<option value=2>简体中文</option>' not in web:
        fail("web settings cannot select Simplified Chinese")
    if "(bytes[0] & 0xf0) == 0xe0" not in outputs:
        fail("OLED UTF-8 decoder does not accept three-byte CJK code points")
    if "marquee" in outputs.lower() or "oled_scroll" in outputs.lower():
        fail("moving/scrolling text is forbidden")

    required_phrases = {
        "功率", "温度", "预设", "信息", "启动", "设置", "时钟",
        "语言", "声音", "显示", "延时", "定时器", "恢复出厂",
        "无锅", "完成", "故障", "暂停", "已连接", "未连接",
    }
    missing_phrases = sorted(required_phrases - set(c_strings(localized_source)))
    if missing_phrases:
        fail(f"missing Chinese UI phrases: {', '.join(missing_phrases)}")

    used_codepoints = {
        ord(char)
        for text in c_strings(localized_source)
        for char in text
        if is_cjk(char)
    }
    font_codepoints = {
        int(value, 16)
        for value in re.findall(r"\{0x([0-9A-Fa-f]{4}), \{", outputs)
    }
    missing_glyphs = sorted(used_codepoints - font_codepoints)
    if missing_glyphs:
        fail("missing OLED glyphs: " + "".join(chr(value) for value in missing_glyphs))

    localized_strings = [
        text for text in c_strings(localized_source)
        if any(is_cjk(char) for char in text)
    ]
    overflow = [(text, static_width(text)) for text in localized_strings
                if static_width(text) > 64]
    if overflow:
        fail("static 1x strings exceed 64px: " +
             ", ".join(f"{text}={width}px" for text, width in overflow))

    print(f"LOCALIZATION CHECK: PASS ({len(used_codepoints)} CJK glyphs, "
          f"{len(set(localized_strings))} static strings, no scrolling)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
