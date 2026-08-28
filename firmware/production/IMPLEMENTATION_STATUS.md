# MCL02M custom firmware — implementation status

Дата: 2026-08-28
Версия исходников: `0.2.7-dev`
Статус: the `0.2.6-dev` app image is currently on the development unit's stock
`ota_1` slot. A supervised test confirmed that Pause retained the session with
`R20=0`, `R26=2` and no false `EPB`, then exposed an incorrect Resume requirement for
the stopped `R26=0` state. Source `0.2.7-dev` fixes that Resume gate and is awaiting an
explicitly authorized flash. The earlier one-time NVS refresh is complete and must
not be repeated automatically.

## Реализованный пользовательский контур

| Блок | Поведение |
|---|---|
| POWER | `0…99`; encoder slow `1`, fast `5`; вращение не запускает нагрев; gear 0 enters the active-zero session rather than full Stop |
| TEMPERATURE | `40…190 °C`; Start above target enters active zero; heating resumes at target minus 3 °C; PREHEAT `56/77/99`, APPROACH/HOLD are capped at `35` and tuned for earlier coast |
| HOLD SATURATED | gear `35` в течение 90 s при ошибке не менее 3 °C: orange, warning и сообщение; предел не повышается |
| Start | только отдельным нажатием центра; силовой preflight и `STARTING` до `R26=02` |
| Pause/Resume | short center enters the same active-zero command while preserving a distinct PAUSED state; timer freezes; Resume does not deliberately Stop/re-arm; 2 h continuous manual Pause performs full Stop |
| Stop/Sleep | hold центра 1,5 s с немедленным срабатыванием и звуком — Stop/Back; следующий hold в Idle — Sleep; Cancel всегда Stop/Back |
| Sleep | default 1 min Idle; wake returns to Home/Power, or Home/Temperature when Sleep began from Temperature; secondary menu selections are never restored; heartbeat and safety continue at zero output |
| OLED | renderer использует полные `64×48` без отладочной рамки; десять 1-bit картинок показывают turn-on 5 s синхронно с boot-мелодией, wake 3 s, cooking 2,5 s, confirm/cancel 1,5 s, ready/no-pan/error до изменения состояния и две 10-s стадии Sleep; active timeout default 3 min; первое нажатие/вращение только будит; encoder guard 1,5 s |
| TIMER SCREEN | `AUTO` полностью гасит OLED; `ALWAYS` оставляет только countdown и перемещает его раз в минуту |
| Cooking timer | крупные редакторы `MM:SS → HH`, максимум 5 h; при остатке ≥1 h рабочий экран показывает `H:MM′`, ниже часа — `MM:SS″`; TIMER открывает редактор, активный timer отключается только подтверждением центра; RAM only |
| Delayed start | крупные `START IN/START AT` (`СТАРТ ЧЕРЕЗ/СТАРТ В`) `HH:MM`; максимум ближайшие 24 h; Cancel или hold центра отменяют; RAM only; timer LED мигает; при NoPan ждёт 60 s |
| Clock | отдельный пункт `CLOCK/ЧАСЫ` показывает идущие `HH:MM`; ручные 24-часовые `HH:MM` без секунд и без flash-write; реальная SNTP-синхронизация всегда приоритетнее ручной установки; после полного снятия питания offline clock снова недействителен |
| Profiles | five NVS profiles with up to five timed POWER/TEMPERATURE cells; POWER gear 0 is an active-zero wait cell and is not subject to the manual-Pause timeout; zero duration skips a cell; total maximum 5 h; Load still requires a separate physical Start |
| Readings | INFO: три строки `VOLT`, `NTC`, `IGBT`; сверху рабочего POWER — NTC, сверху TEMPERATURE — `S…°` и power; без timer опциональный IGBT заменяет context на 2 s после 5 s context; с timer IGBT скрыт, context остаётся; Pause скрывает timer/context |
| Languages | English / Русский / `简体中文`; китайский использует встроенный 8×8 subset из фактически нужных глифов, UTF-8 decoder поддерживает трёхбайтовые code points, все текстовые строки статичны и проходят 64-px width/coverage gate; полноэкранные картинки не содержат языкового текста |
| Sounds | Утверждённые PWM-таблицы: boot, wake, complete, NoPan, critical и sleep; normal duty 50%, sleep около 18%; Main, Timer и Cancel сохраняют UI click, encoder беззвучен, STAGE/WARNING остаются короткими; `SOUND OFF` не отключает NoPan и critical |
| Critical | Stop + latched `error.png` с фактическим E-кодом; один мотив 2,5 s проигрывается дважды на burst, всего 3 bursts с паузами 4 s; ACK немедленно вызывает `sound_stop()`; Sleep запрещён до ACK |
| NoPan | три последовательных отсчёта по 500 ms; отдельный `nopan.png`, orange blink, обязательный цикл `мелодия 2,74 s → тишина 3 s` даже при `SOUND OFF`, timer freeze, окно возврата 60 s, затем E02 fault; возврат/Stop/Pause немедленно прерывает цикл |
| Stock E-groups | известные устойчивые raw-группы сохраняют E03/E04/E05/E07/E08/E10/E12; communication — E09; неизвестные остаются generic |
| I²C debug | Temporary persisted setting, OFF by default; overlays consecutive bad cycles `0…6` in small text at OLED `x=0, y=10`, including the E09 picture; one clean cycle resets it to zero |
| Active-zero debug | Compile-time removable compact UART frames retain full state, `R20…R27`, last `0D/00/0C`, temperatures, faults and counters, plus input and Pause/Resume decisions; authenticated Wi-Fi keeps the readable JSON snapshot |

## Силовой и safety-контур

- Единственный I²C owner — проверенный power-board driver: `0x2A`, 10 kHz,
  heartbeat 500 ms.
- Чтение ограничено selector `0x20…0x2f`; запись — только `0x0d/0x00/0x0c`.
- На boot до любых остальных подсистем выдаётся `ALL OFF`, затем силовой Stop и
  read-only startup probe.
- Ненулевой gear требует валидных `R20/R23/R24/R26`, нормальных NTC и явного Arm.
- Active zero repeats the stock-derived candidate `W0D=81, W00=00, W0C=00` every
  500 ms. Full Stop remains `00/00/00` and is used for completion, Cancel, faults,
  Sleep paths and the two-hour manual-Pause timeout.
- Gear изменяется максимум на 10 за один heartbeat; релейные паузы остаются во
  владении силового MCU.
- E09 is generated locally after six consecutive bad 500-ms I²C cycles. A
  latched power-board fault retransmits the complete Stop sequence every 500 ms.
- Start timeout, unknown power status, active `R26` after Stop, or a temperature
  guard also causes Stop/fault.
- Hard run limit — 5 h. После reset нагрев не восстанавливается.
- High-priority power-control task зарегистрирован в 5-s Task Watchdog с panic/reset;
  после reset boot снова начинается с Stop.
- Вентилятором интерфейсная ESP32 не управляет.
- Fault handlers and LED runtime state never write NVS. Only `settings.c` persists
  explicit settings/profile/Wi-Fi/admin changes; the application never erases NVS
  automatically.

## Wi-Fi и web

- Wi-Fi по умолчанию OFF. Физическое `Setup → Wi-Fi → Power` сохраняет ON/OFF;
  при сохранённом ON после reboot автоматически используется сохранённая сеть.
- Wi-Fi credentials сохраняются отдельными NVS-ключами только после явного
  `Save & connect`, переживают reboot и замену app-образа.
- Setup AP: `MCL02M-SETUP-…`, пароль `12345678`, web `http://192.168.4.1/`.
- AP password отображается в физическом Wi-Fi submenu. Физический `FACTORY` с
  hold центра очищает только custom namespace: admin, сеть, Settings и Profiles;
  штатные Xiaomi NVS/разделы не стираются.
- ESP-IDF Wi-Fi/PHY runtime storage переведён в RAM, отдельной фоновой записи нет.
- NTP: `pool.ntp.org` и `time.google.com`; timezone `UTC-12:00…UTC+14:00`
  применяется сразу без reboot; `CLOCK OK` означает именно подтверждённую SNTP-
  синхронизацию, после которой ручная установка времени не перезаписывает часы.
- Самодостаточная HTML-страница без картинок: read-only state/raw power
  diagnostics, Wi-Fi, Settings по одной строке и редактор пятиэтапных Profiles.
- Admin password: случайная salt + 20 000 SHA-256 rounds; plaintext не хранится.
- Session/CSRF cookie, срок 12 h, задержка после повторных ошибок входа.
- Web API принципиально не имеет Start/Stop/Pause/setpoint/timer/delayed Start.
  Все оперативные команды нагрева доступны только с физической панели; web
  может сохранять Presets, но не загружает и не запускает их.
- HTTP рассчитан только на доверенную домашнюю LAN. Публиковать порт в интернет
  нельзя.

## Что намеренно не реализовано

- Mi Home, MIoT, Xiaomi account/cloud, NFC, штатные recipes и firmware update.
- Многокадровые анимации; текущие картинки являются статическими и не влияют
  на heartbeat или силовой state machine.
- Управление вентилятором или прямой выбор колец.
- Запись eFuse, Secure Boot, Flash Encryption, новая partition table и OTA update.
- Автоматический Start при выборе профиля или после reboot/power loss.

## Оставшиеся ограничения и необязательная характеризация

- Setpoint range is `40…190 °C`. The revised PREHEAT/APPROACH/HOLD controller
  and active-zero command still need supervised cookware/relay characterization.
- Production keeps the 80 °C interface IGBT guard and a separate 210 °C bottom
  emergency cutoff. The power MCU's native E05 remains active.
- Полный перебор редких fault paths и длительный web/network soak могут быть
  выполнены позднее как необязательная характеризация.
