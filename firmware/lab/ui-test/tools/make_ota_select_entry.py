"""Create one ESP-IDF v3 ESP32 OTA select entry without touching the device.

The output is a single 32-byte esp_ota_select_entry_t.  It is intended for
the otherwise-erased second sector of the stock MCL02M otadata partition.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


ENTRY_SIZE = 32
OTA_IMG_VALID = 2


def make_entry(sequence: int) -> bytes:
    sequence_bytes = struct.pack("<I", sequence)
    crc = zlib.crc32(sequence_bytes, 0xFFFFFFFF)
    entry = struct.pack(
        "<I20sII",
        sequence,
        b"\xFF" * 20,
        OTA_IMG_VALID,
        crc,
    )
    assert len(entry) == ENTRY_SIZE
    return entry


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--sequence", type=int, default=2)
    args = parser.parse_args()

    # Cross-check the algorithm against the preserved stock seq=1 entry.
    stock_crc = struct.unpack_from("<I", make_entry(1), 28)[0]
    if stock_crc != 0x4743989A:
        raise SystemExit(f"CRC self-test failed: 0x{stock_crc:08x}")

    entry = make_entry(args.sequence)
    args.output.write_bytes(entry)
    crc = struct.unpack_from("<I", entry, 28)[0]
    print(f"wrote={args.output} bytes={len(entry)} seq={args.sequence} state=2 crc=0x{crc:08x}")


if __name__ == "__main__":
    main()
