# MCL02M UI-only test firmware

Тестовая прошивка для интерфейсной ESP32-платы Xiaomi/Mijia MCL02M (`chunmi.ihcooker.v2`). Она предназначена для экспериментального определения органов управления и индикации перед разработкой собственной прошивки.

## Что она делает

- публикует в UART и WebSocket события центральной кнопки GPIO34, энкодера GPIO5/GPIO14 и двух touch-кнопок;
- по команде из локальной веб-страницы кратко включает выбранные UI-выходы;
- показывает на OLED простые проверочные изображения;
- по команде кратко включает пищалку;
- по отдельной команде читает `R20…R27` силовой платы и показывает raw self-check;
- поднимает временную AP `MCL02M-TEST-xxxx` и умеет подключиться к домашней 2.4-GHz сети без сохранения пароля во flash.

События кнопок и энкодера **только журналируются**. Они не управляют OLED, LED, пищалкой или силовой платой.

## Чего в ней нет

- нет записи управляющих регистров силовой платы `0x0D`, `0x00`, `0x0C`;
- нет start/heat/gear/resume API;
- нет произвольного I²C/GPIO/SPI API;
- нет Mi Home, MIoT, OTA, рецептов и NFC;
- нет NVS-записи Wi-Fi или PHY calibration;
- нет стирания flash, eFuse, Secure Boot или Flash Encryption.

Единственная передача в I²C0 — один selector-байт `0x20…0x2F`, после которого читаются два ответных байта. Это read-only протокол силового MCU.

## Проверенные выводы

| Узел | ESP32 |
|---|---|
| S1 | GPIO34, active-low |
| Encoder phases | GPIO5 / GPIO14 |
| Touch I²C1 | SDA19, SCL18, address 0x60, 10 kHz |
| OLED | SCLK25, MOSI27, RESET26, D/C33, candidate CS0 |
| Serial LED | STB17, CLK16, SDN4 |
| Direct UI outputs | GPIO22 / GPIO32 |
| Buzzer | GPIO23 |
| Power board read-only I²C0 | SDA13, SCL15, address 0x2A, 10 kHz |

## Сборка

Используется ESP-IDF 6.0.2:

```powershell
. "$env:IDF_PATH\export.ps1"
idf.py build
python .\tools\safety_check.py
```

Основной app image: `build/mcl02m_ui_test.bin`. Его размер должен быть не больше штатного OTA-слота `0x160000`.

Текущая проверенная сборка:

| Вариант | Размер | SHA-256 |
|---|---:|---|
| OLED `CS=GPIO0`, geometry diagnostics | 810480 | `b6e84d844dea86609bdf25b84f3c40ddb1f9786e6ebc33a493b2b70270e12e51` |
| OLED `CS=-1` | 801120 | `b8c461d2af3b9802df4c40678834db86719bdab0274f6113f326b4518e835bfd` |

Вариант `sdkconfig.no_cs.defaults` предназначен только для второго OLED-теста, если кандидат `CS=GPIO0` не даст изображения.

Его отдельная проверка:

```powershell
python .\tools\safety_check.py --build-dir build_no_cs --sdkconfig build_no_cs\sdkconfig
```

## Критически важно

Не запускать обычный `idf.py flash`: автоматически созданная команда предлагает записать новый bootloader, partition table и otadata. Для нашего обратимого теста это запрещено.

Ничего ещё не записано в ESP32. После отдельного подтверждения допускается подготовленная запись **только app image в неактивный `ota_1`** и минимальное переключение OTA-записи. Штатный `ota_0`, bootloader и partition table должны остаться нетронутыми.

Практический порядок проверки описан в [TEST_PROTOCOL.md](TEST_PROTOCOL.md).
