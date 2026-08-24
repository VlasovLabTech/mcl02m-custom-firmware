# MCL02M: план безопасной UI test firmware

Дата: 2026-08-22
Цель: проверить все локальные органы управления, OLED, LED и пищалку, не позволяя кнопкам или сети управлять нагревом. I²C силовой платы в первой версии используется только для read-only self-check.

## 1. Жёсткие ограничения v0

1. В бинарнике отсутствуют функции записи управляющих регистров силовой платы `0x0D`, `0x00`, `0x0C`.
2. Единственная разрешённая операция I²C0 — запись адреса читаемого регистра `0x20…0x2F` с последующим чтением двух байтов. Это selector для чтения, не actuator command.
3. Никакое событие `S1`, энкодера или touch-кнопок не вызывает OLED, LED, buzzer или I²C-действие. Input pipeline заканчивается JSON-событием и записью в RAM ring buffer.
4. OLED, LED и buzzer управляются только отдельными remote-test endpoint'ами. У каждого выхода есть автоматический timeout и команда `all_off`.
5. На старте все выходы переводятся в известное безопасное состояние; I²C0 actuator pins не создают команд нагрева.
6. OTA, Mi Home, MIoT, рецепты, облако и NFC отсутствуют.

Дополнительно в коде нужен compile-time guard:

```c
#define MCL02M_HEAT_CONTROL_ENABLED 0
#if MCL02M_HEAT_CONTROL_ENABLED
#error "Heat control is forbidden in UI test firmware"
#endif
```

В API также не должно существовать параметра `gear`, `power`, `start`, `resume` или произвольной записи I²C.

## 2. Архитектура прошивки

Рекомендуемая база — ESP-IDF с небольшими независимыми модулями:

```text
drivers/
  encoder_gpio       GPIO5/GPIO14, quadrature decode
  main_button_gpio   GPIO34, debounce/hold timestamps
  touch_ui           I2C1 0x60 + UART1 fallback
  oled_64x48         SPI 2 MHz, framebuffer 384 bytes
  ui_led_serial      STB17/CLK16/SDN4, raw RAM + named channels
  ui_direct_outputs  GPIO22/GPIO32
  buzzer_gpio23      bounded tone/chirp
  powerboard_ro      I2C0 read 0x20..0x2F only

services/
  event_bus          inputs -> telemetry only
  safety_guard       output timeout, all-off, I2C whitelist
  wifi_sta           local router connection
  http_ws            page, REST commands, WebSocket telemetry
```

Модуль UI не содержит cooking state machine. Назначения `left/right`, цветов и прямых LED сначала хранятся как калибруемые physical channels.

## 3. Wi‑Fi без потери интернета

Основной режим — **STA**: ESP32 подключается к существующей 2.4-GHz сети, получает DHCP-адрес и печатает его в UART. Компьютер остаётся в своей обычной сети и сохраняет интернет. Доступ:

- `http://<device-ip>/`;
- WebSocket `/ws`;
- mDNS-имя `mcl02m-test.local` как удобное дополнение, но IP всегда показывается в UART.

Для первой версии SSID/password вводятся через UART и живут только в RAM до перезагрузки. Так тест не пишет штатный NVS и не меняет сохранённую Xiaomi-конфигурацию. Позже можно добавить собственный отдельный раздел хранения.

Fallback SoftAP допустим только как аварийный путь. Предпочтителен AP+STA, чтобы устройство одновременно оставалось клиентом домашней сети; постоянный AP можно отключить после успешного STA connect.

## 4. Телеметрия

WebSocket отправляет JSON Lines или JSON objects:

```json
{
  "t_ms": 123456,
  "type": "encoder",
  "raw_a": 1,
  "raw_b": 0,
  "delta": 1
}
```

Типы событий:

- `main_button`: raw, pressed/released, duration_ms;
- `encoder`: raw A/B, delta, accumulated position, invalid_transition_count;
- `touch`: transport, raw byte, `TOUCH_A/B/BOTH/NONE`, duration_ms;
- `ui_output`: requested channel, actual shadow state, auto-off reason;
- `oled`: frame counter, SPI error;
- `powerboard_ro`: register, value, checksum_ok, retry count;
- `self_check`: `R23` IGBT channel, `R24` bottom NTC and raw `R20…R27` snapshot.

На веб-странице нужны live-поля, журнал последних 1000 событий и кнопка download JSONL/CSV.

## 5. Remote-test API

Все mutating endpoints требуют случайный session token из UART и работают только в локальной сети.

| Endpoint | Действие | Ограничение |
|---|---|---|
| `POST /api/all-off` | гасит LED, выключает buzzer, очищает OLED | всегда доступен |
| `POST /api/led/raw` | один адрес/бит serial driver | один канал, auto-off 3 s |
| `POST /api/led/power-level` | штатная таблица 0…9 | auto-off 5 s |
| `POST /api/direct-output` | GPIO22 или GPIO32 | только один GPIO за раз, auto-off 2 s |
| `POST /api/wifi-led` | raw bit1 или bit2 | без предварительного имени цвета |
| `POST /api/oled/pattern` | grid, pixels, text test | bounded framebuffer, без произвольного SPI |
| `POST /api/buzzer/chirp` | частота/длительность | 1…4 kHz, максимум 300 ms, cooldown 1 s |
| `GET /api/powerboard/registers` | read-only `0x20…0x2F` | actuator registers недоступны |
| `GET /api/self-check` | снимок `R20…R27`, включая IGBT/NTC | только чтение |

Произвольных GPIO/I²C/SPI endpoint'ов не делать: они обойдут смысл safety layer.

## 6. Этапы проверки

### Этап A — offline build и review

- собрать бинарник без платы;
- автоматически проверить map/symbols, что нет функций `write_0d`, `write_00`, `write_0c`, heat/start/resume API;
- unit-test whitelist I²C: разрешены только selectors `0x20…0x2F`;
- сохранить SHA-256 собранных bootloader/partition/app binaries.

### Этап B — электрическая верификация без силовой платы

- силовая плата физически отключена;
- прозвонить `KA/KB`, OLED CS и общий GND;
- измерить питание UI 5 V и idle levels serial bus;
- не подавать 5 V на GPIO ESP32; убедиться в штатном level compatibility.

### Этап C — input-only

- запустить STA и WebSocket;
- все UI-выходы оставить выключенными;
- по очереди: S1 short/long, encoder CW/CCW slow/fast, left touch, right touch, обе touch вместе;
- назначить фактические `KA/KB` и `TOUCH_LEFT/RIGHT` только после этого лога.

### Этап D — output-only, по одному каналу

1. GPIO22 на 0.5 s, затем GPIO32 на 0.5 s — определить timer/backlight.
2. Serial RAM: перебрать каждый используемый бит с auto-off — составить карту девяти power LED и blue/orange.
3. OLED: reset, checkerboard, рамка 64×48, номера страниц, затем ASCII/кириллический тестовый шрифт.
4. Buzzer: один chirp 100 ms, затем два штатных коротких паттерна.

Во время этого этапа нажатия пользователя только логируются и ничего не переключают.

### Этап E — read-only силовая телеметрия

- подключать силовую плату только после завершения UI-only тестов;
- начать после полного снятия/возврата питания, чтобы силовой MCU гарантированно был в Stop;
- выполнить только чтение `R20…R27`, проверить checksum и self-check `R23/R24`;
- не отправлять heartbeat/control sequence и не создавать ни одного ненулевого gear;
- показать raw и преобразованные температуры в браузере.

Если чтение не отвечает, тест считается неуспешным; прошивка не пытается «разбудить» силовую плату управляющими записями.

## 7. Размещение и восстановление

Полный оригинальный 16-MiB dump сохранён, два чтения совпали; Secure Boot и Flash Encryption выключены, UART download разрешён. Поэтому оригинал восстанавливаем.

Предпочтительная схема после отдельного подтверждения записи:

1. собрать тестовое app image под существующую partition table;
2. записать его в неактивный OTA-раздел `miio_fw2/ota_1`;
3. отдельно переключить `otadata` на тестовый slot;
4. штатный `ota_0` не трогать;
5. для возврата переключить `otadata` обратно и восстановить исходное содержимое `ota_1` из dump.

Полная Wi‑Fi test firmware практически не подходит для `load_ram`: ESP-IDF app использует flash-mapped code/data и заметно больше доступного IRAM/DRAM. Inactive OTA даёт более надёжный и всё ещё обратимый путь. Любая запись flash, включая `ota_1` и `otadata`, выполняется только после отдельного подтверждения пользователя.

## 8. Definition of done

Первая test firmware считается готовой, когда:

- все пять input-классов (`S1`, CW, CCW, left, right, both) устойчиво видны в live telemetry;
- ни один input не изменяет output или I²C;
- физически названы все девять power LED, timer/backlight channel и blue/orange bits;
- OLED показывает проверочный framebuffer без артефактов;
- buzzer выдаёт ограниченные паттерны и гарантированно выключается timeout'ом;
- I²C self-check читает `R23/R24`, но actuator-write test подтверждает отказ для `0x0D/0x00/0x0C`;
- `all-off` и watchdog возвращают безопасное состояние после потери Wi‑Fi/браузера.
