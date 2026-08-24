#!/usr/bin/env python3
"""Parse an ESP-IDF partition table in a full ESP32 flash dump.

This tool only reads the input dump and writes extracted files to a local
output directory. It never communicates with the ESP32.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


ENTRY_SIZE = 32
TABLE_SECTOR_SIZE = 0x1000
PARTITION_MAGIC = b"\xAA\x50"
MD5_MAGIC = b"\xEB\xEB"
END_MAGIC = b"\xFF\xFF"


APP_SUBTYPES = {
    0x00: "factory",
    **{0x10 + index: f"ota_{index}" for index in range(16)},
    0x20: "test",
}

DATA_SUBTYPES = {
    0x00: "ota",
    0x01: "phy",
    0x02: "nvs",
    0x03: "coredump",
    0x04: "nvs_keys",
    0x05: "efuse",
    0x06: "undefined",
    0x81: "fat",
    0x82: "spiffs",
    0x83: "littlefs",
}


@dataclass(frozen=True)
class Partition:
    index: int
    label: str
    type: int
    type_name: str
    subtype: int
    subtype_name: str
    offset: int
    size: int
    end: int
    flags: int
    encrypted: bool
    sha256: str
    filename: str


def type_name(value: int) -> str:
    if value == 0x00:
        return "app"
    if value == 0x01:
        return "data"
    return f"custom_0x{value:02x}"


def subtype_name(part_type: int, value: int) -> str:
    if part_type == 0x00:
        return APP_SUBTYPES.get(value, f"app_0x{value:02x}")
    if part_type == 0x01:
        return DATA_SUBTYPES.get(value, f"data_0x{value:02x}")
    return f"0x{value:02x}"


def safe_label(label: str, index: int) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", label).strip("._")
    return cleaned or f"partition_{index:02d}"


def parse_candidate(data: bytes, table_offset: int) -> tuple[list[dict], dict]:
    table = data[table_offset : table_offset + TABLE_SECTOR_SIZE]
    if len(table) != TABLE_SECTOR_SIZE:
        raise ValueError("partition table sector extends beyond the dump")

    raw_entries: list[dict] = []
    md5_record: dict | None = None
    terminator_offset: int | None = None

    for relative in range(0, TABLE_SECTOR_SIZE, ENTRY_SIZE):
        entry = table[relative : relative + ENTRY_SIZE]
        magic = entry[:2]

        if magic == PARTITION_MAGIC:
            _, part_type, subtype, offset, size, raw_label, flags = struct.unpack(
                "<2sBBLL16sL", entry
            )
            label = raw_label.split(b"\0", 1)[0].decode("utf-8", errors="replace")
            raw_entries.append(
                {
                    "label": label,
                    "type": part_type,
                    "subtype": subtype,
                    "offset": offset,
                    "size": size,
                    "flags": flags,
                }
            )
            continue

        if magic == MD5_MAGIC:
            stored = entry[16:32]
            calculated = hashlib.md5(table[:relative]).digest()
            md5_record = {
                "relative_offset": relative,
                "stored": stored.hex(),
                "calculated": calculated.hex(),
                "valid": stored == calculated,
            }
            continue

        if magic == END_MAGIC:
            terminator_offset = relative
            break

        raise ValueError(
            f"unexpected entry magic {magic.hex()} at table + 0x{relative:x}"
        )

    if not raw_entries:
        raise ValueError("no partition entries")

    for entry in raw_entries:
        if entry["size"] <= 0:
            raise ValueError(f"partition {entry['label']!r} has an empty size")
        if entry["offset"] + entry["size"] > len(data):
            raise ValueError(f"partition {entry['label']!r} exceeds dump bounds")

    metadata = {
        "table_offset": table_offset,
        "table_sector_size": TABLE_SECTOR_SIZE,
        "entry_count": len(raw_entries),
        "terminator_relative_offset": terminator_offset,
        "md5": md5_record,
    }
    return raw_entries, metadata


def find_partition_table(data: bytes) -> tuple[list[dict], dict]:
    candidates = [0x8000]
    candidates.extend(
        offset for offset in range(0x9000, min(len(data), 0x100000), 0x1000)
    )
    errors: list[str] = []
    for offset in candidates:
        if data[offset : offset + 2] != PARTITION_MAGIC:
            continue
        try:
            entries, metadata = parse_candidate(data, offset)
        except ValueError as exc:
            errors.append(f"0x{offset:x}: {exc}")
            continue
        if len(entries) >= 2:
            return entries, metadata
    suffix = f" ({'; '.join(errors)})" if errors else ""
    raise ValueError(f"no valid ESP-IDF partition table found{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    data = args.dump.read_bytes()
    full_sha256 = hashlib.sha256(data).hexdigest()
    raw_entries, metadata = find_partition_table(data)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    table_offset = metadata["table_offset"]
    table_path = args.output_dir / f"partition_table_0x{table_offset:08x}.bin"
    table_path.write_bytes(data[table_offset : table_offset + TABLE_SECTOR_SIZE])

    bootloader_offset = 0x1000
    if table_offset <= bootloader_offset:
        raise ValueError("partition table is not after the normal ESP32 bootloader")
    bootloader_path = args.output_dir / (
        f"bootloader_0x{bootloader_offset:08x}_0x{table_offset - bootloader_offset:08x}.bin"
    )
    bootloader_bytes = data[bootloader_offset:table_offset]
    bootloader_path.write_bytes(bootloader_bytes)

    partitions: list[Partition] = []
    for index, entry in enumerate(raw_entries):
        label = safe_label(entry["label"], index)
        subtype = subtype_name(entry["type"], entry["subtype"])
        filename = (
            f"{index:02d}_{label}_{type_name(entry['type'])}_{subtype}_"
            f"0x{entry['offset']:08x}_0x{entry['size']:08x}.bin"
        )
        payload = data[entry["offset"] : entry["offset"] + entry["size"]]
        (args.output_dir / filename).write_bytes(payload)
        partitions.append(
            Partition(
                index=index,
                label=entry["label"],
                type=entry["type"],
                type_name=type_name(entry["type"]),
                subtype=entry["subtype"],
                subtype_name=subtype,
                offset=entry["offset"],
                size=entry["size"],
                end=entry["offset"] + entry["size"],
                flags=entry["flags"],
                encrypted=bool(entry["flags"] & 0x01),
                sha256=hashlib.sha256(payload).hexdigest(),
                filename=filename,
            )
        )

    manifest = {
        "source": str(args.dump.resolve()),
        "source_size": len(data),
        "source_sha256": full_sha256,
        "partition_table": {
            **metadata,
            "filename": table_path.name,
            "sha256": hashlib.sha256(table_path.read_bytes()).hexdigest(),
        },
        "bootloader": {
            "offset": bootloader_offset,
            "size": len(bootloader_bytes),
            "filename": bootloader_path.name,
            "sha256": hashlib.sha256(bootloader_bytes).hexdigest(),
        },
        "partitions": [asdict(partition) for partition in partitions],
    }

    manifest_path = args.output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    csv_path = args.output_dir / "partitions.csv"
    with csv_path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(asdict(partitions[0]).keys()))
        writer.writeheader()
        writer.writerows(asdict(partition) for partition in partitions)

    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
