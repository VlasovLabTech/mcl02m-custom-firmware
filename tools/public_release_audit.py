#!/usr/bin/env python3
"""Fail if public project files contain known private or release-unsafe data."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_DIRS = {"_local_private", ".git"}
FORBIDDEN_NAMES = {".env", "credentials.json", "secrets.json"}
FORBIDDEN_SUFFIXES = {".bin", ".elf", ".map", ".sr", ".pcap", ".pcapng", ".dump", ".dmp", ".nvs"}
TEXT_SUFFIXES = {".c", ".h", ".cpp", ".py", ".md", ".txt", ".html", ".csv", ".json", ".yml", ".yaml", ".cmake", ""}

PATTERNS = {
    "personal Windows path": re.compile(r"C:[\\/]Users[\\/]User", re.I),
    "known private SSID": re.compile(r"\bSvet\b"),
    "known account id": re.compile(r"\b1633236962\b"),
    "private email": re.compile(r"\bysvlasov@|@yandex\.ru\b", re.I),
    "OpenAI key": re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}\b"),
    "GitHub token": re.compile(r"\bgh[opsu]_[A-Za-z0-9]{20,}\b"),
    "AWS access key": re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    "Xiaomi token assignment": re.compile(r"(?i)\btoken\s*[:=]\s*['\"]?[0-9a-f]{32}['\"]?"),
    "unit-specific MAC": re.compile(r"(?i)\be4:fe:43:50:cc:de\b"),
}


def files_to_check() -> list[Path]:
    if (ROOT / ".git").exists():
        result = subprocess.run(
            ["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True
        )
        return [ROOT / item.decode("utf-8") for item in result.stdout.split(b"\0") if item]
    return [
        path for path in ROOT.rglob("*")
        if path.is_file() and not any(part in PRIVATE_DIRS for part in path.relative_to(ROOT).parts)
    ]


def main() -> None:
    failures: list[str] = []
    checked = 0
    for path in files_to_check():
        relative = path.relative_to(ROOT)
        lower_name = path.name.lower()
        if lower_name in FORBIDDEN_NAMES or path.suffix.lower() in FORBIDDEN_SUFFIXES:
            failures.append(f"forbidden public artifact: {relative}")
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES and path.name not in {"CMakeLists.txt", "LICENSE"}:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        checked += 1
        for label, pattern in PATTERNS.items():
            if pattern.search(text):
                failures.append(f"{label}: {relative}")
    if failures:
        print("PUBLIC RELEASE AUDIT FAILED", file=sys.stderr)
        for failure in sorted(set(failures)):
            print(f" - {failure}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS: {checked} public text files; no known personal paths, credentials, tokens, or unsafe artifacts")


if __name__ == "__main__":
    main()
