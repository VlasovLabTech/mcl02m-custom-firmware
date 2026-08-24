#!/usr/bin/env python3
"""Small dependency-free client for the MCL02M power-stage test firmware."""

from __future__ import annotations

import argparse
import json
import os
import time
import urllib.error
import urllib.request


def request(host: str, path: str, token: str | None = None, post: bool = False) -> dict:
    url = f"http://{host.rstrip('/')}{path}"
    headers = {"X-Test-Token": token} if token else {}
    req = urllib.request.Request(url, headers=headers, method="POST" if post else "GET")
    try:
        with urllib.request.urlopen(req, timeout=3) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise SystemExit(f"HTTP {exc.code}: {body}") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="mcl02m-test.local")
    parser.add_argument("--token", default=os.environ.get("MCL02M_POWER_TOKEN"))
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    sub.add_parser("stop")
    sub.add_parser("arm").add_argument("--ms", type=int, default=30_000)
    start = sub.add_parser("start")
    start.add_argument("--gear", type=int, required=True)
    start.add_argument("--ms", type=int, required=True)
    gear = sub.add_parser("gear")
    gear.add_argument("value", type=int)
    sub.add_parser("pause")
    sub.add_parser("resume")
    sub.add_parser("clear-fault")
    gap = sub.add_parser("hb-gap")
    gap.add_argument("--ms", type=int, default=3_000)
    watch = sub.add_parser("watch")
    watch.add_argument("--seconds", type=float, default=60)

    args = parser.parse_args()
    if args.command == "status":
        result = request(args.host, "/api/power/status")
    elif args.command == "stop":
        result = request(args.host, "/api/power/stop", post=True)
    elif args.command == "watch":
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            status = request(args.host, "/api/power/status")
            print(json.dumps(status, ensure_ascii=False), flush=True)
            time.sleep(0.5)
        return 0
    else:
        if not args.token:
            raise SystemExit("A token is required (--token or MCL02M_POWER_TOKEN).")
        paths = {
            "arm": f"/api/power/arm?ms={args.ms}",
            "start": f"/api/power/start?gear={args.gear}&ms={args.ms}",
            "gear": f"/api/power/gear?gear={args.value}",
            "pause": "/api/power/pause",
            "resume": "/api/power/resume",
            "clear-fault": "/api/power/clear-fault",
            "hb-gap": f"/api/power/hb-gap?confirm=1&ms={args.ms}",
        }
        result = request(args.host, paths[args.command], args.token, post=True)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
