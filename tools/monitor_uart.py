#!/usr/bin/env python3
"""Passively capture and summarize MCL02M compact UART diagnostics.

The monitor never writes to the serial port. Every received UART line is preserved
with a host timestamp; the console shows event frames plus changed `D` summaries.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
from pathlib import Path
import re
import sys
import time


ANSI = re.compile(r"\x1b\[[0-9;]*m")
FRAME_TYPES = {"B", "C", "D", "E", "F", "I", "L", "N", "P", "S", "T", "U", "W", "X", "Z"}


def available_ports() -> list[str]:
    from serial.tools import list_ports

    return [port.device for port in list_ports.comports()]


def compact_payload(line: str) -> str | None:
    clean = ANSI.sub("", line).strip()
    candidate = clean.rsplit(": ", 1)[-1]
    if len(candidate) >= 2 and candidate[0] in FRAME_TYPES and candidate[1] == ",":
        return candidate
    return None


def parse_d(frame: str) -> dict[str, str] | None:
    fields = next(csv.reader([frame]))
    if len(fields) < 69 or fields[0] != "D":
        return None
    return {
        "state": fields[1],
        "target": fields[2],
        "applied": fields[3],
        "topology": fields[4],
        "cmd": f"{fields[5]}/{fields[6]}/{fields[7]}",
        "r20": fields[8],
        "r26": fields[14],
        "mask": fields[16],
        "igbt": fields[17],
        "bottom": fields[18],
        "bad": fields[26],
        "limited": fields[32],
        "r28": fields[33],
        "fault": fields[36],
        "lease_ms": fields[45],
        "transition": fields[50],
        "pending": fields[51],
        "transmitted": fields[52],
        "requested_state": fields[53],
        "requested_gear": fields[54],
        "confirmed_state": fields[59],
        "confirmed_gear": fields[60],
        "feedback": fields[63],
        "rejection": fields[68],
    }


def d_summary(status: dict[str, str]) -> str:
    return (
        "D "
        f"state={status['state']} target/applied={status['target']}/{status['applied']} "
        f"topology={status['topology']} cmd={status['cmd']} "
        f"R20/R26/R28={status['r20']}/{status['r26']}/{status['r28']} "
        f"temp={status['bottom']}C igbt={status['igbt']}C bad={status['bad']} "
        f"limit={status['limited']} lease={status['lease_ms']}ms "
        f"tx={status['transition']}:{status['pending']}/{status['transmitted']} "
        f"requested={status['requested_state']}:{status['requested_gear']} "
        f"confirmed={status['confirmed_state']}:{status['confirmed_gear']} "
        f"feedback={status['feedback']} fault={status['fault']} "
        f"reject={status['rejection']}"
    )


def important_d(status: dict[str, str], previous: dict[str, str] | None) -> bool:
    if previous is None:
        return True
    watched = (
        "state", "target", "applied", "topology", "cmd", "r20", "r26",
        "mask", "bad", "limited", "fault", "transition", "pending",
        "transmitted", "requested_state", "requested_gear", "confirmed_state",
        "confirmed_gear", "feedback", "rejection",
    )
    return any(status[key] != previous[key] for key in watched)


def default_output() -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path("_local_private") / "validation" / f"uart-{stamp}.log"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--minutes", type=float, default=15.0,
                        help="capture duration; 0 runs until Ctrl+C")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--verbose", action="store_true",
                        help="print every UART line in addition to saving it")
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("pyserial is required (python -m pip install pyserial)", file=sys.stderr)
        return 2

    ports = available_ports()
    if args.list:
        print("\n".join(ports) if ports else "No serial ports found")
        return 0
    if not args.port:
        print("Port is required. Available: " + (", ".join(ports) if ports else "none"),
              file=sys.stderr)
        return 2

    output = (args.output or default_output()).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    deadline = None if args.minutes == 0 else time.monotonic() + args.minutes * 60.0
    previous_d: dict[str, str] | None = None

    print(f"Passive monitor: {args.port} @ {args.baud}; log: {output}")
    try:
        connection = serial.Serial(port=None, baudrate=args.baud, timeout=0.2,
                                   write_timeout=0, dsrdtr=False, rtscts=False)
        connection.dtr = False
        connection.rts = False
        connection.port = args.port
        connection.open()
        with connection, output.open("w", encoding="utf-8", newline="\n") as log:
            log.write(f"# opened={datetime.now().astimezone().isoformat()} "
                      f"port={args.port} baud={args.baud} passive=true\n")
            while deadline is None or time.monotonic() < deadline:
                raw = connection.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                stamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
                log.write(f"{stamp} {line}\n")
                log.flush()
                frame = compact_payload(line)
                if frame is None:
                    if args.verbose:
                        print(line)
                    continue
                if frame.startswith("D,"):
                    status = parse_d(frame)
                    if status is None:
                        print(f"MALFORMED {frame}")
                    elif args.verbose or important_d(status, previous_d):
                        print(d_summary(status))
                    if status is not None:
                        previous_d = status
                else:
                    print(frame)
    except KeyboardInterrupt:
        print("Monitor stopped by user")
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
