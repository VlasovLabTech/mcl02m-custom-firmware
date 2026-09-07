#!/usr/bin/env python3
"""Run and record one supervised cookware sweep over the special ESP32 image."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
import json
from pathlib import Path
import re
import statistics
import sys
import time


ANSI = re.compile(r"\x1b\[[0-9;]*m")
POINTS = (0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 25, 30,
          35, 36, 40, 45, 50, 55, 56, 60, 70, 80, 90, 99)
REGISTER_NAMES = tuple(f"r{register:02x}" for register in range(0x20, 0x30))
DATA_FIELDS = (
    "host_time", "host_elapsed_s", "pan", "phase", "requested_power",
    "actual_power", "limited", "fw_ms", "cycle", "state", "target",
    "applied", "topology", "cmd_0d", "cmd_00", "cmd_0c",
    "read_attempt_mask", "read_error_mask", "write_attempt_mask",
    "write_error_mask", "valid_mask", *REGISTER_NAMES,
    "igbt_c", "bottom_c", "bad_cycles", "consecutive_bad_cycles", "fault",
)


def payload(line: str) -> str | None:
    clean = ANSI.sub("", line).strip()
    at = clean.find("X,")
    return clean[at:] if at >= 0 else None


def parse_d(frame: str) -> dict[str, str] | None:
    fields = next(csv.reader([frame]))
    if len(fields) != 38 or fields[:2] != ["X", "D"]:
        return None
    row = {
        "fw_ms": fields[2], "cycle": fields[3], "state": fields[4],
        "target": fields[5], "applied": fields[6], "topology": fields[7],
        "cmd_0d": fields[8], "cmd_00": fields[9], "cmd_0c": fields[10],
        "read_attempt_mask": fields[11], "read_error_mask": fields[12],
        "write_attempt_mask": fields[13], "write_error_mask": fields[14],
        "valid_mask": fields[15],
    }
    row.update({name: fields[16 + index]
                for index, name in enumerate(REGISTER_NAMES)})
    row.update({
        "igbt_c": fields[32], "bottom_c": fields[33],
        "bad_cycles": fields[34], "consecutive_bad_cycles": fields[35],
        "limited": fields[36], "fault": fields[37],
    })
    return row


def default_output(pan: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path("_local_private") / "validation" / "power-sweeps" / f"{stamp}-{pan}"


def summary_rows(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    output = []
    for point in POINTS:
        group = [row for row in rows
                 if row["phase"] == "dwell" and
                 int(row["requested_power"]) == point]
        values = [int(row["r21"], 16) for row in group
                  if int(row["valid_mask"], 16) & (1 << 1)]
        output.append({
            "requested_power": point,
            "actual_power": (int(group[-1]["actual_power"]) if group else None),
            "limited": (int(group[-1]["limited"]) if group else None),
            "samples": len(group),
            "r21_valid_samples": len(values),
            "r21_min": min(values) if values else None,
            "r21_median": round(statistics.median(values), 2) if values else None,
            "r21_mean": round(statistics.fmean(values), 2) if values else None,
            "r21_max": max(values) if values else None,
            "r20_values": "/".join(sorted({row["r20"] for row in group})),
            "r26_values": "/".join(sorted({row["r26"] for row in group})),
            "r27_values": "/".join(sorted({row["r27"] for row in group})),
            "igbt_c_min": min((int(row["igbt_c"]) for row in group), default=None),
            "igbt_c_max": max((int(row["igbt_c"]) for row in group), default=None),
            "bottom_c_min": min((int(row["bottom_c"]) for row in group), default=None),
            "bottom_c_max": max((int(row["bottom_c"]) for row in group), default=None),
            "read_error_frames": sum(int(row["read_error_mask"], 16) != 0
                                     for row in group),
            "write_error_frames": sum(int(row["write_error_mask"], 16) != 0
                                      for row in group),
        })
    return output


def write_summary(directory: Path, rows: list[dict[str, str]], result: str) -> None:
    summary = summary_rows(rows)
    with (directory / "summary.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    (directory / "summary.json").write_text(
        json.dumps({"result": result, "points": summary}, indent=2) + "\n",
        encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial port, for example COM5")
    parser.add_argument("--pan", required=True,
                        help="short ASCII cookware label (letters, digits, - or _)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout-min", type=float, default=12.0)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,23}", args.pan):
        parser.error("--pan must match [A-Za-z0-9_-]{1,23}")
    try:
        import serial
    except ImportError:
        print("pyserial is required", file=sys.stderr)
        return 2

    directory = (args.output or default_output(args.pan)).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    rows: list[dict[str, str]] = []
    started = time.monotonic()
    deadline = started + args.timeout_min * 60
    next_ping = started
    phase = "preflight"
    requested = ""
    actual = ""
    limited = "0"
    result = "HOST_TIMEOUT"
    start_sent = False
    print(f"Sweep {args.pan}: {args.port} @ {args.baud}; output: {directory}")

    connection = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                               write_timeout=1, dsrdtr=False, rtscts=False)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        with connection, (directory / "raw_uart.log").open(
                "w", encoding="utf-8", newline="\n") as raw_log, \
             (directory / "samples.csv").open(
                "w", encoding="utf-8", newline="") as csv_stream:
            writer = csv.DictWriter(csv_stream, fieldnames=DATA_FIELDS)
            writer.writeheader()
            connection.write(b"X,PING\n")
            while time.monotonic() < deadline:
                now = time.monotonic()
                if now >= next_ping:
                    connection.write(b"X,PING\n")
                    next_ping = now + 1.0
                raw = connection.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                host_time = datetime.now().astimezone().isoformat(timespec="milliseconds")
                raw_log.write(f"{host_time} {line}\n")
                raw_log.flush()
                frame = payload(line)
                if frame is None:
                    continue
                fields = next(csv.reader([frame]))
                if fields[:2] == ["X", "PONG"]:
                    if not start_sent and len(fields) >= 3 and fields[2] == "READY":
                        connection.write(f"X,START,{args.pan}\n".encode("ascii"))
                        start_sent = True
                    continue
                if fields[:2] == ["X", "P"] and len(fields) >= 4:
                    requested = fields[2]
                    if fields[3] == "SET":
                        phase = "settle"
                        print(f"P{requested}: settling", flush=True)
                    elif fields[3] == "BEGIN" and len(fields) == 6:
                        phase, actual, limited = "dwell", fields[4], fields[5]
                        suffix = f" (limited to P{actual})" if limited == "1" else ""
                        print(f"P{requested}: 15 s{suffix}", flush=True)
                    elif fields[3] == "END":
                        phase = "between"
                    continue
                parsed = parse_d(frame)
                if parsed is not None:
                    row = {
                        "host_time": host_time,
                        "host_elapsed_s": f"{now - started:.3f}",
                        "pan": args.pan, "phase": phase,
                        "requested_power": requested,
                        "actual_power": actual, **parsed,
                    }
                    rows.append(row)
                    writer.writerow(row)
                    csv_stream.flush()
                    continue
                if fields[:2] in (["X", "DONE"], ["X", "ABORTED"]):
                    result = ",".join(fields[1:])
                    print(frame, flush=True)
                    break
                if fields[:2] != ["X", "ACK"]:
                    print(frame, flush=True)
    except KeyboardInterrupt:
        result = "HOST_ABORT"
        try:
            connection.write(b"X,ABORT\n")
            time.sleep(0.5)
        except Exception:
            pass
        print("Abort requested", file=sys.stderr)
    except serial.SerialException as exc:
        result = f"SERIAL_ERROR:{exc}"
        print(result, file=sys.stderr)
    finally:
        write_summary(directory, rows, result)
    return 0 if result.startswith("DONE,") else 1


if __name__ == "__main__":
    raise SystemExit(main())
