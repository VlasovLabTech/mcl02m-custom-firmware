#!/usr/bin/env python3
"""Read-only integrity and metadata check for an ESP32 MCL02M flash dump.

This tool deliberately does not decode or print NVS/minvs values.  Those
partitions may contain Wi-Fi credentials, Xiaomi tokens, account IDs, and
other personal data.  It is intended to make a reproducible, share-safe
summary of a dump that has already been acquired.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional


PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SECTOR = 0x1000
PARTITION_MAGIC = 0x50AA
PARTITION_END = 0xFFFF
APP_MAGIC = 0xE9

SENSITIVE_LABELS = {"nvs", "minvs", "coredump"}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def parse_partitions(dump: bytes) -> List[Dict[str, Any]]:
    """Parse ESP-IDF partition entries without depending on esptool."""
    result: List[Dict[str, Any]] = []
    off = PARTITION_TABLE_OFFSET
    end = off + PARTITION_TABLE_SECTOR
    while off + 32 <= end:
        magic = struct.unpack_from("<H", dump, off)[0]
        if magic == PARTITION_END:
            break
        if magic != PARTITION_MAGIC:
            off += 32
            continue
        ptype, subtype = struct.unpack_from("<BB", dump, off + 2)
        poff, size = struct.unpack_from("<II", dump, off + 4)
        raw_label = dump[off + 12 : off + 28]
        label = raw_label.split(b"\0", 1)[0].decode("ascii", "replace")
        flags = struct.unpack_from("<I", dump, off + 28)[0]
        body = dump[poff : poff + size]
        result.append(
            {
                "label": label,
                "type": ptype,
                "subtype": subtype,
                "offset": poff,
                "size": size,
                "end": poff + size,
                "flags": flags,
                "sha256": sha256_bytes(body),
                "non_ff": sum(b != 0xFF for b in body),
                "non_ff_pct": round(100.0 * sum(b != 0xFF for b in body) / size, 3)
                if size
                else 0.0,
            }
        )
        off += 32
    return result


def app_header(dump: bytes, offset: int) -> Optional[Dict[str, Any]]:
    if offset + 24 > len(dump) or dump[offset] != APP_MAGIC:
        return None
    segment_count = dump[offset + 1]
    flash_mode = dump[offset + 2]
    flash_size_freq = dump[offset + 3]
    entry = struct.unpack_from("<I", dump, offset + 4)[0]
    return {
        "offset": offset,
        "segment_count": segment_count,
        "flash_mode": flash_mode,
        "flash_size_nibble": flash_size_freq >> 4,
        "flash_freq_nibble": flash_size_freq & 0x0F,
        "entry": f"0x{entry:08x}",
        "secure_boot_digest_present": bool(dump[offset + 23] & 0x01),
    }


def safe_ascii_hits(path: Path, needles: Iterable[str]) -> List[str]:
    """Return only explicitly allow-listed markers from a Ghidra CSV."""
    hits: List[str] = []
    if not path.exists():
        return hits
    wanted = tuple(needles)
    with path.open("r", encoding="utf-8", errors="replace", newline="") as handle:
        for row in csv.DictReader(handle):
            value = row.get("value", "")
            if any(needle.lower() in value.lower() for needle in wanted):
                hits.append(value.replace("\\n", "\\n"))
    return hits


def build_summary(first: Path, second: Optional[Path], strings: Optional[Path]) -> Dict[str, Any]:
    first_bytes = first.read_bytes()
    summary: Dict[str, Any] = {
        "dump": {
            "path": str(first),
            "size": len(first_bytes),
            "sha256": sha256_bytes(first_bytes),
        },
        "partitions": parse_partitions(first_bytes),
    }
    if second:
        second_hash = sha256_file(second)
        summary["second_dump"] = {
            "path": str(second),
            "size": second.stat().st_size,
            "sha256": second_hash,
            "matches_first": second_hash == summary["dump"]["sha256"],
        }
    app_parts = [p for p in summary["partitions"] if p["label"] in {"miio_fw1", "miio_fw2"}]
    summary["app_headers"] = [app_header(first_bytes, p["offset"]) for p in app_parts]
    summary["sensitive_partitions_omitted"] = sorted(SENSITIVE_LABELS)
    if strings:
        summary["safe_markers"] = safe_ascii_hits(
            strings,
            (
                "chunmi.ihcooker.v2",
                "miio_app",
                "P_3_7_Temperature",
                "E_3_6_Igbtsensorhigh",
                "E_3_10_Wirebroken",
                "UartListenTask",
                "CookStepTask",
            ),
        )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--second-dump", type=Path)
    parser.add_argument("--ghidra-strings", type=Path)
    parser.add_argument("--json", type=Path, help="write machine-readable summary")
    args = parser.parse_args()
    summary = build_summary(args.dump, args.second_dump, args.ghidra_strings)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    public = json.loads(json.dumps(summary))
    public["dump"].pop("path", None)
    if "second_dump" in public:
        public["second_dump"].pop("path", None)
    for part in public["partitions"]:
        if part["label"] in SENSITIVE_LABELS:
            part.pop("sha256", None)
            part.pop("non_ff", None)
            part.pop("non_ff_pct", None)
            part["sensitive"] = True
    print(json.dumps(public, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
