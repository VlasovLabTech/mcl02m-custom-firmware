#!/usr/bin/env python3
"""Offline Chinese localization/font coverage and static-layout gate."""

from __future__ import annotations

import re
import sys
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
OUTPUTS = ROOT.parents[1] / "firmware" / "lab" / "ui-test" / "main" / "ui_outputs.c"
MANUAL = ROOT.parents[1] / "docs" / "user-manual.html"


class ManualPanelParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.panels: dict[str, list[str]] = {}
        self.current: str | None = None
        self.depth = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = dict(attrs)
        if self.current is None and tag == "div" and attributes.get("data-lang-panel"):
            self.current = attributes["data-lang-panel"]
            self.panels.setdefault(self.current, [])
            self.depth = 1
        elif self.current is not None and tag == "div":
            self.depth += 1

    def handle_endtag(self, tag: str) -> None:
        if self.current is None or tag != "div":
            return
        self.depth -= 1
        if self.depth == 0:
            self.current = None

    def handle_data(self, data: str) -> None:
        if self.current is not None:
            self.panels[self.current].append(data)


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def c_strings(source: str) -> list[str]:
    return re.findall(r'"((?:\\.|[^"\\])*)"', source)


def is_cjk(character: str) -> bool:
    return 0x3400 <= ord(character) <= 0x9FFF


def is_cyrillic(character: str) -> bool:
    return 0x0400 <= ord(character) <= 0x04FF


def static_width(text: str) -> int:
    """Mirror the regular 1x OLED layout: CJK=8px, other glyphs=5px, gap=1px."""
    if not text:
        return 0
    return sum(8 if is_cjk(char) else 5 for char in text) + len(text) - 1


def compact_width(text: str) -> int:
    """Width after the OLED renderer removes inter-glyph gaps to make text fit."""
    return sum(8 if is_cjk(char) else 5 for char in text)


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
        "固件", "版本", "功率板", "警告", "未知状态", "按任意键",
        "待启动", "停止",
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

    russian_strings = [
        text for text in c_strings(localized_source)
        if any(is_cyrillic(char) for char in text)
    ]
    russian_overflow = [(text, compact_width(text)) for text in russian_strings
                        if compact_width(text) > 64]
    if russian_overflow:
        fail("Russian strings exceed the compact 64px fallback: " +
             ", ".join(f"{text}={width}px" for text, width in russian_overflow))

    forbidden_fallbacks = (
        'tr(language, "VERSION", "ВЕРСИЯ", "VERSION")',
        'tr(lang, "FIRMWARE", "ПРОШИВКА", "FIRMWARE")',
        'tr(lang, "PWR BOARD", "СИЛ ПЛАТА", "PWR BOARD")',
    )
    if any(fallback in localized_source for fallback in forbidden_fallbacks):
        fail("Chinese firmware/version labels still fall back to English")
    if '"WARNING", "UNKNOWN", r20, "PRESS", "ANY KEY"' in display:
        fail("R20 warning is still hard-coded in English")

    manual = MANUAL.read_text(encoding="utf-8")
    parser = ManualPanelParser()
    parser.feed(manual)
    if set(parser.panels) != {"en", "ru", "zh"}:
        fail("user manual does not contain exactly EN/RU/ZH language panels")
    if "data:image/png;base64," not in manual or re.search(r'<img[^>]+src="https?://', manual):
        fail("user manual is not a self-contained offline HTML document")
    for language in ("en", "ru", "zh"):
        if len(re.findall(rf'<section id="{language}-', manual)) != 10:
            fail(f"user manual {language} panel does not contain all ten sections")
    manual_required = {
        "en": ("40…190", "210", "P<36", "Small cookware", "Hot surface",
               "Public and private sounds", "TIME / NOT SET", "eight-hour",
               "128-second", "one minute", "short or long center press", "R20", "EST", "ECL", "ETM",
               "R21/R25/R27", "320-ms", "80 °C", "92 °C", "98 °C",
               "seven seconds", "retried once", "Cold-Start ramp", "0.2.33-dev"),
        "ru": ("40…190", "210", "P<36", "Маленькая посуда",
               "Горячая поверхность", "Публичные и приватные звуки",
               "ВРЕМЯ / НЕ ЗАДАНО", "восьмичасовой", "128-секундная", "одну минуту", "короткое или длинное нажатие центра",
               "R20", "EST", "ECL", "ETM", "R21/R25/R27", "320 мс", "80 °C", "92 °C",
               "98 °C", "семь секунд", "ещё одну попытку", "Плавный холодный Start", "0.2.33-dev"),
        "zh": ("40…190", "210", "P<36", "小锅具", "热表面",
               "公开和私有声音", "时间 / 未设置", "8小时", "128秒", "一分钟", "短按中键、长按中键",
               "R20", "EST", "ECL", "ETM", "R21/R25/R27", "320毫秒", "80 °C", "92 °C",
               "98 °C", "隐藏七秒", "再试一次", "冷启动渐升", "0.2.33-dev"),
    }
    for language, phrases in manual_required.items():
        panel_text = " ".join(" ".join(parser.panels[language]).split())
        if "175" in panel_text:
            fail(f"user manual {language} panel still contains the obsolete 175 C limit")
        missing = [phrase for phrase in phrases if phrase not in panel_text]
        if missing:
            fail(f"user manual {language} panel is stale: {', '.join(missing)}")

    print(f"LOCALIZATION CHECK: PASS ({len(used_codepoints)} CJK glyphs, "
          f"{len(set(localized_strings))} Chinese strings, "
          f"{len(set(russian_strings))} Russian strings, trilingual manual, no scrolling)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
