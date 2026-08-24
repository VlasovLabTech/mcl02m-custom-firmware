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

## Physical access and connection photographs

> **Removing the interface board is not recommended.** The rotary encoder makes
> board removal extremely difficult. On the unit documented here, the author
> instead used a regular utility knife to cut away a small section of the plastic
> housing, working carefully and gradually, step by step. This provided access
> to the programming area without removing the board. Disconnect the appliance
> from mains power before doing any mechanical work, and avoid damaging the PCB,
> wiring, or nearby components.

![Interface board with the plastic housing carefully cut away](interface-board-access.jpg)

![Close-up of the ESP32 programming connections](programming-connections.jpg)

Wire colors are only illustrative; use the labeled test points and the mapping
above when making connections.

> **Mains warning:** use the interface board completely disconnected from the
> power board, or use a verified isolated USB/data/power path. A normal USB-UART
> adapter is not isolation.
