#!/usr/bin/env python3
"""Validate the self-contained trilingual user manual without external packages."""

from __future__ import annotations

import base64
from html.parser import HTMLParser
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
MANUAL = ROOT / "docs" / "user-manual.html"


class ManualParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: list[str] = []
        self.hrefs: list[str] = []
        self.panels: list[str] = []
        self.images: list[str] = []
        self.external: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if values.get("id"):
            self.ids.append(values["id"] or "")
        if values.get("data-lang-panel"):
            self.panels.append(values["data-lang-panel"] or "")
        if tag == "a" and values.get("href"):
            href = values["href"] or ""
            self.hrefs.append(href)
            if re.match(r"^(?:https?:)?//", href):
                self.external.append(href)
        if tag in {"img", "script", "link", "source"}:
            uri = values.get("src") or values.get("href")
            if uri:
                if tag == "img":
                    self.images.append(uri)
                if re.match(r"^(?:https?:)?//", uri):
                    self.external.append(uri)


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    text = MANUAL.read_text(encoding="utf-8")
    parser = ManualParser()
    parser.feed(text)
    parser.close()

    if sorted(parser.panels) != ["en", "ru", "zh"]:
        fail(f"language panels are {parser.panels!r}")
    duplicates = sorted({item for item in parser.ids if parser.ids.count(item) > 1})
    if duplicates:
        fail(f"duplicate ids: {duplicates}")
    missing = sorted({href[1:] for href in parser.hrefs if href.startswith("#") and href[1:] not in parser.ids})
    if missing:
        fail(f"missing local anchors: {missing}")
    if parser.external:
        fail(f"external resources/links found: {parser.external}")
    if len(parser.images) != 1 or not parser.images[0].startswith("data:image/png;base64,"):
        fail("the tiger must be the single Base64-embedded PNG")
    payload = parser.images[0].split(",", 1)[1]
    try:
        png = base64.b64decode(payload, validate=True)
    except Exception as exc:  # pragma: no cover - diagnostic path
        fail(f"invalid embedded Base64: {exc}")
    if not png.startswith(b"\x89PNG\r\n\x1a\n"):
        fail("embedded image is not a PNG")
    for token in ("Mi Home", "PRESET", "ПРЕСЕТ", "预设", "E09", "12345678"):
        if token not in text:
            fail(f"required manual content is missing: {token}")
    print(
        f"PASS: {MANUAL.name}; {len(text):,} chars; "
        f"panels=en/ru/zh; {len(parser.ids)} ids; PNG={len(png):,} bytes; offline"
    )


if __name__ == "__main__":
    main()
