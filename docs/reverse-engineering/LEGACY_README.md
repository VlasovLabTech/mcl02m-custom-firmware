# MCL02M ESP32 reverse engineering

Статический анализ полного 16-MiB flash dump интерфейсной ESP32 Xiaomi/Mijia
Induction Cooker 2 MCL02M (`chunmi.ihcooker.v2`).

## Правила хранения

- Оригинальные dump-файлы никогда не изменяются.
- `private/`, `work/`, `tools/` и `ghidra_projects/` исключены из Git.
- NVS, minvs, device tokens, Wi-Fi credentials, certificates и private keys не
  копируются в публичные отчёты.
- Отслеживаемые отчёты используют уровни уверенности:
  `confirmed`, `high confidence`, `hypothesis`, `unknown`.

## Структура

- `reports/` — обезличенные технические отчёты.
- `scripts/` — воспроизводимые парсеры и декодеры.
- `private/` — исходные/извлечённые чувствительные бинарники, игнорируется Git.
- `work/` — временные дизассемблерные и индексные артефакты, игнорируется Git.
- `tools/` — локально загруженные сторонние инструменты, игнорируется Git.
- `ghidra_projects/` — локальная база Ghidra, игнорируется Git.

## Основные отчёты

- `reports/MCL02M_static_reverse_engineering.md` — flash, partitions и общий статический разбор.
- `reports/MCL02M_local_control_analysis.md` — локальная cooking/safety/UI-логика.
- `reports/MCL02M_I2C_PROTOCOL.md` — проверенная runtime-спецификация силового I²C.
- `reports/MCL02M_UI_HARDWARE_MAP.md` — GPIO, разъёмы, OLED, touch, encoder, LED и buzzer.
- `reports/MCL02M_UI_TEST_FIRMWARE_PLAN.md` — план изолированной test firmware без управления нагревом.
