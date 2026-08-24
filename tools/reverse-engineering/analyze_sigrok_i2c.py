#!/usr/bin/env python3
"""Offline I2C decoder for a sigrok .sr v2 capture.

The decoder is deliberately read-only: it opens a capture archive, decodes
the two selected digital probes, and writes a JSON report.  It tries both
CH0/CH1 polarities (SCL/SDA and SDA/SCL) because the physical probe order is
not known at first.  It does not access OpenBench or any instrument.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import statistics
import zipfile
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Tuple


CHUNK_RE = re.compile(r"^logic-1-(\d+)$")


@dataclass
class Frame:
    start_sample: int
    stop_sample: Optional[int] = None
    bytes: List[int] = field(default_factory=list)
    acks: List[bool] = field(default_factory=list)
    incomplete: bool = False

    def as_dict(self, sample_rate: int) -> Dict[str, object]:
        address = None
        direction = None
        if self.bytes:
            address = self.bytes[0] >> 1
            direction = "read" if (self.bytes[0] & 1) else "write"
        return {
            "start_sample": self.start_sample,
            "start_s": self.start_sample / sample_rate,
            "stop_sample": self.stop_sample,
            "stop_s": None if self.stop_sample is None else self.stop_sample / sample_rate,
            "address_7bit": address,
            "address_hex": None if address is None else f"0x{address:02x}",
            "direction": direction,
            "bytes_hex": " ".join(f"{b:02x}" for b in self.bytes),
            "bytes": self.bytes,
            "acks": self.acks,
            "ack_count": sum(self.acks),
            "byte_count": len(self.bytes),
            "incomplete": self.incomplete,
        }


class Decoder:
    def __init__(self, sample_rate: int, scl_bit: int, sda_bit: int) -> None:
        self.sample_rate = sample_rate
        self.scl_bit = scl_bit
        self.sda_bit = sda_bit
        self.prev_scl: Optional[int] = None
        self.prev_sda: Optional[int] = None
        self.sample_index = -1
        self.active = False
        self.frame: Optional[Frame] = None
        self.bit_count = 0
        self.current_byte = 0
        self.awaiting_ack = False
        self.frames: List[Frame] = []
        self.starts = 0
        self.stops = 0
        self.scl_rises = 0
        self.scl_falls = 0
        self.rise_samples: List[int] = []
        self.transitions = Counter()
        self.state_counts = Counter()

    def feed(self, sample: int) -> None:
        scl = (sample >> self.scl_bit) & 1
        sda = (sample >> self.sda_bit) & 1
        self.sample_index += 1
        self.state_counts[(scl, sda)] += 1
        if self.prev_scl is None:
            self.prev_scl, self.prev_sda = scl, sda
            return
        if scl != self.prev_scl or sda != self.prev_sda:
            self.transitions[(self.prev_scl, self.prev_sda, scl, sda)] += 1

        start = self.prev_sda == 1 and sda == 0 and scl == 1
        stop = self.prev_sda == 0 and sda == 1 and scl == 1
        if start:
            self.starts += 1
            if self.frame is not None:
                self.frame.incomplete = True
                self.frames.append(self.frame)
            self.frame = Frame(self.sample_index)
            self.active = True
            self.bit_count = 0
            self.current_byte = 0
            self.awaiting_ack = False
        elif stop:
            self.stops += 1
            if self.frame is not None:
                self.frame.stop_sample = self.sample_index
                self.frames.append(self.frame)
            self.frame = None
            self.active = False
            self.bit_count = 0
            self.current_byte = 0
            self.awaiting_ack = False

        if self.prev_scl == 0 and scl == 1:
            self.scl_rises += 1
            self.rise_samples.append(self.sample_index)
            if self.active and self.frame is not None:
                if self.awaiting_ack:
                    self.frame.acks.append(sda == 0)
                    self.awaiting_ack = False
                    self.bit_count = 0
                    self.current_byte = 0
                else:
                    self.current_byte = (self.current_byte << 1) | sda
                    self.bit_count += 1
                    if self.bit_count == 8:
                        self.frame.bytes.append(self.current_byte)
                        self.awaiting_ack = True
        elif self.prev_scl == 1 and scl == 0:
            self.scl_falls += 1
        self.prev_scl, self.prev_sda = scl, sda

    def finish(self) -> None:
        if self.frame is not None:
            self.frame.incomplete = True
            self.frames.append(self.frame)
            self.frame = None

    def report(self) -> Dict[str, object]:
        frames = [f.as_dict(self.sample_rate) for f in self.frames]
        valid_acks = sum(int(v) for f in self.frames for v in f.acks)
        valid_bytes = sum(len(f.bytes) for f in self.frames)
        plausible_addresses = sum(
            1
            for f in self.frames
            if f.bytes and 0x03 <= (f.bytes[0] >> 1) <= 0x77
        )
        score = valid_acks * 4 + valid_bytes + plausible_addresses * 3 + len(self.frames)
        rise_intervals = [
            b - a for a, b in zip(self.rise_samples, self.rise_samples[1:])
            if 20 <= (b - a) <= 2_000
        ]
        clock_stats = {}
        if rise_intervals:
            clock_stats = {
                "edge_intervals_samples_count": len(rise_intervals),
                "edge_interval_min_us": min(rise_intervals) * 1_000_000 / self.sample_rate,
                "edge_interval_median_us": statistics.median(rise_intervals) * 1_000_000 / self.sample_rate,
                "edge_interval_mean_us": statistics.mean(rise_intervals) * 1_000_000 / self.sample_rate,
                "edge_interval_max_us": max(rise_intervals) * 1_000_000 / self.sample_rate,
                "approx_clock_hz_from_median": self.sample_rate / statistics.median(rise_intervals),
            }
        return {
            "mapping": {"scl": f"CH{self.scl_bit}", "sda": f"CH{self.sda_bit}"},
            "score": score,
            "sample_rate_hz": self.sample_rate,
            "sample_count": self.sample_index + 1,
            "duration_s": (self.sample_index + 1) / self.sample_rate,
            "starts": self.starts,
            "stops": self.stops,
            "scl_rises": self.scl_rises,
            "scl_falls": self.scl_falls,
            "clock_timing": clock_stats,
            "state_counts": {"%d%d" % k: v for k, v in sorted(self.state_counts.items())},
            "transition_count": sum(self.transitions.values()),
            "frames": frames,
            "valid_ack_bits": valid_acks,
            "decoded_bytes": valid_bytes,
            "plausible_address_frames": plausible_addresses,
        }


def parse_samplerate(metadata: str) -> int:
    m = re.search(r"samplerate=(\d+)\s*([kKmMgG]?)Hz", metadata)
    if not m:
        raise ValueError("samplerate not found in sigrok metadata")
    multiplier = {"": 1, "k": 1_000, "K": 1_000, "m": 1_000_000, "M": 1_000_000,
                  "g": 1_000_000_000, "G": 1_000_000_000}[m.group(2)]
    return int(m.group(1)) * multiplier


def chunk_names(zf: zipfile.ZipFile) -> List[str]:
    names = []
    for name in zf.namelist():
        m = CHUNK_RE.match(name)
        if m:
            names.append((int(m.group(1)), name))
    return [name for _, name in sorted(names)]


def decode_archive(path: Path, scl_bit: int, sda_bit: int) -> Dict[str, object]:
    with zipfile.ZipFile(path) as zf:
        metadata = zf.read("metadata").decode("utf-8", errors="replace")
        sample_rate = parse_samplerate(metadata)
        decoder = Decoder(sample_rate, scl_bit, sda_bit)
        total_bytes = 0
        for name in chunk_names(zf):
            data = zf.read(name)
            total_bytes += len(data)
            if len(data) % 2:
                raise ValueError(f"odd chunk length: {name}")
            for (sample,) in struct.iter_unpack("<H", data):
                decoder.feed(sample)
        decoder.finish()
        report = decoder.report()
        report["source"] = str(path)
        report["archive_metadata"] = metadata
        report["raw_bytes"] = total_bytes
        report["chunks"] = len(chunk_names(zf))
        return report


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    ap.add_argument("--json", type=Path, help="write full JSON report")
    ap.add_argument("--quiet", action="store_true", help="do not print the full report")
    args = ap.parse_args()
    reports = [decode_archive(args.capture, 0, 1), decode_archive(args.capture, 1, 0)]
    reports.sort(key=lambda r: int(r["score"]), reverse=True)
    result = {"capture": str(args.capture), "mappings": reports, "best": reports[0]}
    if args.json:
        args.json.write_text(json.dumps(result, indent=2), encoding="utf-8")
    if not args.quiet:
        print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
