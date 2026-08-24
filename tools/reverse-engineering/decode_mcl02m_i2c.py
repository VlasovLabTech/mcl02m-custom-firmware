#!/usr/bin/env python3
"""Decode passive MCL02M I2C register samples; never transmits anything.

Input is CSV rows with ``reg,value,checksum`` (decimal or 0x-prefixed).  A
logic-analyser export can be reduced to this three-column form without
including the device's Wi-Fi/NVS data.  The firmware accepts both the full
sum and its low byte, so both checks are reported.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from typing import Any, Dict, Iterable, List


REG_NAMES = {
    0x20: "status_or_error_code",
    0x21: "raw_channel_21",
    0x22: "raw_channel_22",
    0x23: "temperature_channel_23_mapped_when_ready",
    0x24: "temperature_channel_24_mapped_to_miot_temperature",
    0x25: "aux_channel_25",
    0x26: "aux_channel_26",
    0x27: "aux_channel_27",
    0x28: "aux_channel_28",
    0x29: "aux_channel_29",
    0x2A: "aux_channel_2a",
    0x2B: "aux_channel_2b",
    0x2C: "aux_channel_2c",
    0x2D: "aux_channel_2d",
    0x2E: "aux_channel_2e",
    0x2F: "aux_channel_2f",
}


def number(value: str) -> int:
    return int(value.strip(), 0)


def decode_rows(rows: Iterable[Dict[str, str]]) -> List[Dict[str, Any]]:
    decoded: List[Dict[str, Any]] = []
    for row in rows:
        reg = number(row["reg"])
        value = number(row["value"]) & 0xFF
        checksum = number(row["checksum"]) & 0xFF
        expected_full = value + reg
        decoded.append(
            {
                "reg": f"0x{reg:02x}",
                "value": value,
                "checksum": checksum,
                "expected_sum": expected_full,
                "checksum_valid": checksum == expected_full or checksum == (expected_full & 0xFF),
                "field": REG_NAMES.get(reg, "unknown_register"),
            }
        )
    return decoded


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="?", type=argparse.FileType("r"), default=sys.stdin)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    rows = csv.DictReader(args.csv)
    decoded = decode_rows(rows)
    if args.json:
        print(json.dumps(decoded, indent=2))
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=list(decoded[0]) if decoded else
                                ["reg", "value", "checksum", "expected_sum", "checksum_valid", "field"])
        writer.writeheader()
        writer.writerows(decoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
