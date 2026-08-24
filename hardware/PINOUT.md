# Interface Board Pinout

## ESP32 UART bootloader test points

```text
USB-UART GND  -> GND
USB-UART TX   -> RXD0
USB-UART RX   -> TXD0
USB-UART 3V3 logic levels only
GPIO0 -> GND while EN is pulsed to enter download mode
```

Available test points: `GND`, `3V3`, `EN`, `GPIO0`, `RXD0`, `TXD0`.

`3V3` is a 3.3 V rail/reference. **Never feed 5 V into it.** During development,
5 V board power was supplied through the stock low-voltage panel connector while
the interface board was disconnected from the power board.

## Panel connectors

```text
Connector 1: STB, CLK, SDN, 5V
Connector 2: GND, S1, KA, KB
```

| Panel signal | ESP32 |
|---|---:|
| STB | GPIO17 |
| CLK | GPIO16 |
| SDN | GPIO4 |
| S1 | GPIO34 |
| KA / KB | GPIO5 / GPIO14 |

## Other confirmed signals

| Function | ESP32 |
|---|---:|
| Power I²C SDA / SCL | GPIO13 / GPIO15 |
| Touch I²C SDA / SCL | GPIO19 / GPIO18 |
| OLED CS / SCLK / MOSI | GPIO0 / GPIO25 / GPIO27 |
| OLED RESET / D-C | GPIO26 / GPIO33 |
| Buzzer | GPIO23 |
| Direct UI output | GPIO22 |
| Unknown, held LOW | GPIO32 |

Connection photographs will be added in this directory later.

> **Mains warning:** use the interface board completely disconnected from the
> power board, or use a verified isolated USB/data/power path. A normal USB-UART
> adapter is not isolation.
