# Local web API

API предназначен только для web-страницы самой плиты в доверенной LAN.
Все ответы — JSON. POST использует
`Content-Type: application/x-www-form-urlencoded`.

## Авторизация

- `POST /api/setup`, поле `password` — однократное задание admin password,
  минимум 8 символов.
- `POST /api/login`, поле `password` — выдаёт cookie `MCLSESSION` и JSON-поле
  `csrf`.
- Все последующие endpoints требуют session cookie; POST дополнительно требует
  header `X-CSRF-Token`.
- Одна сессия действует 12 h. После пяти неверных паролей включается возрастающая
  задержка до 30 s.

## Статус

`GET /api/status` возвращает:

- firmware version;
- cooking state/mode/fault, selected/applied gear, temperature, timer/schedule;
- raw power-board `R20…R27`, I²C cycle/error counters, topology, Stop/start
  confirmation;
- STA/AP/NTP state;
- текущие Settings и пять Profiles по пять timed cells;
- NVS availability и dropped telemetry count.

Endpoint read-only: он не обращается к I²C напрямую, а копирует последний
snapshot единственного power-board driver.

## Нагрев через web запрещён

В прошивке нет `/api/control` и нет web-обработчиков Start, Stop, Pause,
изменения мощности/температуры, cooking timer или delayed Start. Запуск и всё
оперативное управление нагревом выполняются только физическими органами панели.

Web-интерфейс показывает read-only состояние плиты и позволяет подготовить или
изменить Presets. Сохранение Preset само по себе не загружает его в активный
режим и никогда не запускает нагрев.

## Настройки и данные

- `POST /api/wifi`: `ssid`, `password`.
- `POST /api/settings`: `language`, `sound`, `context`, `igbt`, `timer_screen`,
  `sleep_clock`,
  `sleep`, `timezone` и `oled` в секундах: `60`, `120`, `180`, `300`, `600`,
  `1200`, `1800`, `3600`, `7200`, `10800`, `14400` или `18000`.
- `POST /api/profile`: `index`, `name` и для каждой ячейки `1…5` поля
  `modeN`, `gearN`, `tempN` (`40…190`) и `timeN` в минутах. `timeN=0`
  пропускает ячейку; суммарно не более 300 минут.

Wi-Fi по умолчанию OFF и включается только физически из подменю плиты. При ON
сохранённые STA credentials применяются автоматически после reboot. Factory reset
доступен только с физической панели и удаляет custom admin/Wi-Fi/Settings/Profiles.

Cooking timer, delayed Start, active state и telemetry во flash не пишутся.
Persistent writes разрешены только модулю `settings.c` после явного Save.
