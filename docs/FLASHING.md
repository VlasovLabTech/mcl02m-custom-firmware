# Flashing and Recovery

This document describes the safe release workflow. It is not permission to work
on an energized mains appliance.

## Required isolation

Use one of these two arrangements:

1. Remove/disconnect the interface board from the mains-connected power board and
   power it separately with 5 V through the stock low-voltage connector.
2. Use a verified galvanically isolated USB path that isolates both data and
   power. Common modules use an isolated DC/DC converter plus an ADuM3160-class
   USB isolator; verify the actual module and ratings.

Never connect a normal USB ground or earth-referenced instrument to an unknown
internal power-board ground. `3V3` is not a 5 V power input.

## Enter ROM download mode

1. Connect GND.
2. Cross UART: adapter TX → `RXD0`, adapter RX → `TXD0`.
3. Use 3.3 V UART logic.
4. Pull `GPIO0` low.
5. Pulse `EN` low, then high.
6. Release `GPIO0` after the bootloader is active.

## Build and validate

```powershell
cd firmware\production
idf.py set-target esp32
idf.py build
python tests\safety_check.py
python tests\localization_check.py
python -m esptool --chip esp32 image_info build\mcl02m_custom.bin
```

Verify that the application is less than `0x160000` bytes.

## Intended write scope

The examined stock layout uses:

```text
ota_0  0x010000  size 0x160000
ota_1  0x170000  size 0x160000
```

Development writes targeted only the app image at `0x170000`. Do not use a
merged image by default. Do not write bootloader, partition table, `otadata`,
NVS, PHY, `ota_0`, or eFuse without a separate reviewed recovery plan.

Illustrative command — substitute the verified serial port and do not run it
without owner approval:

```powershell
python -m esptool --chip esp32 --port COMx --baud 921600 write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x170000 build\mcl02m_custom.bin
```

## First boot after a write

1. Reset with no heat request.
2. Confirm boot, OLED, inputs, LEDs, buzzer, and zero-output I²C heartbeat.
3. Confirm Stop and plausible read-only voltage/temperature values.
4. Only then perform short supervised water-load tests.
5. Keep the operator at the cooker and retain a physical means to remove power.
6. After a test, allow the power-board fan to complete cooldown.

Keep a verified original full-flash dump outside Git. Never publish device NVS,
MAC addresses, cloud tokens, account data, or local credentials.
