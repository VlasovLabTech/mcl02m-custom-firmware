#!/usr/bin/env python3
"""Offline safety/build audit for the MCL02M UI-only test application."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
MAIN = PROJECT / "main"
ORIGINAL = PROJECT.parents[1] / "reverse_engineering" / "private"
OTA_SLOT_SIZE = 0x160000
EXPECTED_DUMP_SHA256 = "e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--sdkconfig", default="sdkconfig")
    args = parser.parse_args()
    build = (PROJECT / args.build_dir).resolve()
    sdkconfig_path = (PROJECT / args.sdkconfig).resolve()

    sources = {path: path.read_text(encoding="utf-8") for path in MAIN.rglob("*") if path.suffix in {".c", ".h"}}
    combined = "\n".join(sources.values())
    safety = sources[MAIN / "safety.h"]
    powerboard = sources[MAIN / "powerboard_ro.c"]
    web = sources[MAIN / "web_server.c"]

    require(re.search(r"#define\s+MCL02M_HEAT_CONTROL_ENABLED\s+0\b", safety) is not None,
            "compile-time heat guard is not zero")
    require("#error \"Heat control is forbidden" in safety, "compile-time #error guard is missing")
    require(combined.count("i2c_master_transmit(") == 1,
            "expected exactly one I2C transmit call in the whole application")
    require(powerboard.count("i2c_master_transmit(") == 1,
            "the sole I2C transmit must live in powerboard_ro.c")
    require("i2c_master_transmit_receive(" not in combined,
            "combined I2C write/read API is forbidden in this test")
    require("mcl02m_powerboard_read_selector_allowed(reg)" in powerboard,
            "power-board selector whitelist is missing")
    require("i2c_master_transmit(s_device, &reg, 1, 50)" in powerboard,
            "read transaction must transmit exactly one selector byte")
    require("MCL02M_POWERBOARD_READ_MIN = 0x20" in safety and
            "MCL02M_POWERBOARD_READ_MAX = 0x2f" in safety,
            "read whitelist must remain 0x20..0x2f")

    for endpoint in ("/api/start", "/api/heat", "/api/gear", "/api/resume", "/api/i2c"):
        require(endpoint not in web, f"forbidden endpoint found: {endpoint}")
    for flash_api in ("esp_partition_write", "esp_ota_", "nvs_set_", "spi_flash_write", "esp_flash_write"):
        require(flash_api not in combined, f"flash-write API found: {flash_api}")

    sdkconfig = sdkconfig_path.read_text(encoding="utf-8")
    require("# CONFIG_ESP_WIFI_NVS_ENABLED is not set" in sdkconfig,
            "Wi-Fi NVS storage must be disabled")
    require("# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set" in sdkconfig,
            "PHY calibration storage must be disabled")
    require("# CONFIG_SECURE_BOOT is not set" in sdkconfig, "test build must not enable Secure Boot")
    require("# CONFIG_FLASH_ENCRYPTION_ENABLED is not set" in sdkconfig,
            "test build must not enable Flash Encryption")
    cs_match = re.search(r"CONFIG_MCL02M_OLED_CS_GPIO=(-?\d+)", sdkconfig)
    require(cs_match is not None and int(cs_match.group(1)) in {0, -1},
            "OLED CS must be the reviewed GPIO0 candidate or the no-CS variant")

    app = build / "mcl02m_ui_test.bin"
    table = build / "partition_table" / "partition-table.bin"
    stock_table = ORIGINAL / "partitions" / "partition_table_0x00008000.bin"
    dump = ORIGINAL / "source" / "mcl02m_esp32_flash_working.bin"
    for path in (app, table, stock_table, dump):
        require(path.is_file(), f"required file missing: {path}")
    require(app.stat().st_size <= OTA_SLOT_SIZE,
            f"app is too large for the 0x160000-byte OTA slot: {app.stat().st_size}")
    require(table.read_bytes() == stock_table.read_bytes()[: table.stat().st_size],
            "generated partition table entries differ from the stock table")
    require(dump.stat().st_size == 0x1000000, "original dump is not 16 MiB")
    require(sha256(dump) == EXPECTED_DUMP_SHA256, "original recovery dump hash mismatch")

    print("SAFETY CHECK: PASS")
    print("heat control: compiled out")
    print("power-board I2C transmit calls: 1 (read selector only, 0x20..0x2f)")
    print("flash-writing APIs in application: none")
    print("Wi-Fi/NVS and PHY flash storage: disabled")
    print(f"app size: {app.stat().st_size} / {OTA_SLOT_SIZE} bytes")
    print(f"app SHA-256: {sha256(app)}")
    print(f"OLED CS: {cs_match.group(1)}")
    print(f"stock dump SHA-256: {sha256(dump)}")
    print("partition entries: byte-identical to stock")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"SAFETY CHECK: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
