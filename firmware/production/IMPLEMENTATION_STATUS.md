# MCL02M custom firmware — implementation status

Дата: 2026-08-29
Версия исходников: `0.2.20-dev`
Статус: the development unit runs the hash-verified `0.2.17-dev` app written only to
stock `ota_1` at `0x170000` on 2026-08-29. It booted successfully, and supervised
testing confirmed retained-session active zero without unwanted relay switching,
Sleep/Wake, the temporary I2C debug display, and temperature operation. At a 125 °C
empty-pan setpoint, initial heating overshot by approximately
5 °C and subsequent holding was accurate. The source includes adaptive initial
braking, pause-safe output recomputation, and direct low/high topology crossing. Its
physical Settings menu exposes both the compile-time firmware version and live raw
power-board `R28` on a dedicated four-line, left-aligned OLED screen. The
unflashed `0.2.15-dev` source excludes the temporary I2C-loss counter from the
production menu and OLED while preserving that implementation behind a disabled
compile-time flag. Internal E09 counting and compact diagnostics are unchanged. The
unflashed `0.2.16-dev` source also replaces the queued timer toggle with synchronous
Set/Disable and uses a seconds, minutes, hours confirmation sequence. The
deployed `0.2.17-dev` source completes the Start/EST evidence package: Start cannot
be confirmed by stale or deadline-late `R26`, and an EST preserves the exact first
cause in RAM, compact UART and authenticated status. The deterministic host model
covers the complete known Start response matrix. Unflashed `0.2.18-dev` implements
the transactional Stop package: all Stop origins converge on `STOPPING`, complete
zero commands continue until two fresh `R26=00` samples, and late recovery remains
possible after timeout or I²C loss without losing the first recorded cause. The
unflashed `0.2.19-dev` source adds a generation-tagged three-second cooking lease.
Only the cooking task renews it in live sessions; the independent power task expires
it into transactional Stop and retains a distinct `ECL / COOK LEASE` diagnosis. The
unflashed `0.2.20-dev` source implements confirmed Start, active zero, Pause and
Resume transactions. Requested, transmitted, feedback-observed and confirmed state
are distinct; a transition completes only after the matching command and a later
fresh compatible `R20/R26` sample, with generation checks, bounded deadlines and
exact rejection evidence. Active-zero confirmation remains an explicitly documented
inference because the power board does not report the selected gear. The
earlier one-time NVS refresh is complete and must not be repeated automatically.

## Реализованный пользовательский контур

| Блок | Поведение |
|---|---|
| POWER | `0…99`; encoder slow `1`, fast `5`; вращение не запускает нагрев; gear 0 enters the active-zero session rather than full Stop |
| SMALL COOKWARE | `R26=01` is accepted as normal heating, not a fault; every control mode is capped at real gear `35` and forced to `A1`; POWER shows the permitted value, rejects upward edits above 35 with a 3-s explanation, and still allows downward edits; `R26=02` clears the restriction |
| VERSION / BOARD | the physical firmware screen has four left-aligned rows: firmware label/version and live raw power-board `R28 XX`; invalid startup data is shown as `R28 --` |
| UNKNOWN R20 | `2B`, `29`, and `2A` remain silent nonfaults; another unrecognized nonzero value shows a persistent `WARNING / UNKNOWN / R20 XX`; the first physical input dismisses only the warning and the established session continues |
| TEMPERATURE | `40…190 °C`; entering T°C immediately copies and clamps the setpoint into editor-owned state; PREHEAT uses `56/77/99`; braking reserve is `clamp(10 °C + positive four-second rise, 10…20 °C)`, with a 15 °C minimum from 170 °C and 5 °C phase hysteresis; APPROACH uses `8 + 2 × error`; PI uses `4 + 2 × error + 0.08 × integral`; APPROACH/HOLD remain capped at `35`; output is active zero at/above target and PI resumes one degree below |
| HOLD SATURATED | gear `35` в течение 90 s при ошибке не менее 3 °C: orange, warning и сообщение; предел не повышается |
| Start | only a separate center press; power preflight and generation-tagged `STARTING` remain pending until fresh compatible `R20` plus `R26=01/02` arrive after the matching successful heartbeat and strictly before the sole 8-s deadline; EST latches repeated Stop plus immutable first-cause evidence |
| Pause/Resume | short center requests a generation-tagged transition and repeated presses remain idempotent until confirmation; Pause is confirmed from fresh retained-session feedback before its timer/state change; temperature trend sampling continues while PI is frozen; Resume recomputes and stages current output before its matching 3-s confirmation window, without Stop/re-arm; 2 h continuous confirmed manual Pause performs full Stop |
| Stop/Sleep | hold центра 1,5 s с немедленным срабатыванием и звуком — Stop/Back; следующий hold в Idle — Sleep; Cancel всегда Stop/Back |
| Sleep | default 1 min Idle; wake returns to Home/Power, or Home/Temperature when Sleep began from Temperature; secondary menu selections are never restored; heartbeat and safety continue at zero output |
| OLED | renderer использует полные `64×48` без отладочной рамки; десять 1-bit картинок показывают turn-on 5 s синхронно с boot-мелодией, wake 3 s, cooking 2,5 s, confirm/cancel 1,5 s, ready/no-pan/error до изменения состояния и две 10-s стадии Sleep; active timeout default 3 min; первое нажатие/вращение только будит; encoder guard 1,5 s |
| TIMER SCREEN | `AUTO` полностью гасит OLED; `ALWAYS` оставляет только countdown и перемещает его раз в минуту |
| Cooking timer | large `MM:SS` editor confirms blinking seconds first and blinking minutes second, followed by a separate hours confirmation; maximum 5 h; Set and Disable complete synchronously, an all-zero value is rejected, and rapid disable/re-enable cannot race queued toggles; at ≥1 h the running screen shows `H:MM′`, below one hour `MM:SS″`; RAM only |
| Delayed start | крупные `START IN/START AT` (`СТАРТ ЧЕРЕЗ/СТАРТ В`) `HH:MM`; максимум ближайшие 24 h; Cancel или hold центра отменяют; RAM only; timer LED мигает; при NoPan ждёт 60 s |
| Clock | отдельный пункт `CLOCK/ЧАСЫ` показывает идущие `HH:MM`; ручные 24-часовые `HH:MM` без секунд и без flash-write; реальная SNTP-синхронизация всегда приоритетнее ручной установки; после полного снятия питания offline clock снова недействителен |
| Profiles | five NVS profiles with up to five timed POWER/TEMPERATURE cells; POWER gear 0 is an active-zero wait cell and is not subject to the manual-Pause timeout; zero duration skips a cell; total maximum 5 h; Load still requires a separate physical Start |
| Readings | INFO: три строки `VOLT`, `NTC`, `IGBT`; сверху рабочего POWER — NTC, сверху TEMPERATURE — `S…°` и power; без timer опциональный IGBT заменяет context на 2 s после 5 s context; с timer IGBT скрыт, context остаётся; Pause скрывает timer/context |
| Languages | English / Русский / `简体中文`; китайский использует встроенный 8×8 subset из фактически нужных глифов, UTF-8 decoder поддерживает трёхбайтовые code points, все текстовые строки статичны и проходят 64-px width/coverage gate; полноэкранные картинки не содержат языкового текста |
| Sounds | Утверждённые PWM-таблицы: boot, wake, complete, NoPan, critical и sleep; normal duty 50%, sleep около 18%; Main, Timer и Cancel сохраняют UI click, encoder беззвучен, STAGE/WARNING остаются короткими; `SOUND OFF` не отключает NoPan и critical |
| Critical | Stop + latched `error.png` с фактическим E-кодом; один мотив 2,5 s проигрывается дважды на burst, всего 3 bursts с паузами 4 s; ACK немедленно вызывает `sound_stop()`; Sleep запрещён до ACK |
| NoPan | три последовательных отсчёта по 500 ms; отдельный `nopan.png`, orange blink, обязательный цикл `мелодия 2,74 s → тишина 3 s` даже при `SOUND OFF`, timer freeze, окно возврата 60 s, затем E02 fault; возврат/Stop/Pause немедленно прерывает цикл |
| Stock E-groups | известные устойчивые raw-группы сохраняют E03/E04/E05/E07/E08/E10/E12; communication — E09; неизвестные остаются generic |
| I²C debug | The internal consecutive-bad-cycle counter, E09 threshold and diagnostic transport remain active. The temporary physical setting and OLED `0…6` overlay are compiled out of the production build with `COOKER_I2C_DEBUG_DISPLAY_ENABLED=0`; their source and two-second peak-hold implementation remain available for a future diagnostic build |
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
- Gear changes by at most 10 per heartbeat inside a topology. When a target lies in
  `1…35` or `56…99`, the boundary crossing is directly `35 ↔ 56`, avoiding transient
  C1 commands and leaving relay sequencing with the power MCU. Explicit manual POWER
  targets `36…55` remain valid.
- Restricted-cookware feedback `R26=01` immediately caps the lower-layer target and
  every cooking-engine output at gear 35, forces `A1`, and cannot be bypassed by
  TEMPERATURE, profiles, retained-session Resume, or a direct POWER edit.
- E09 is generated locally after six consecutive bad 500-ms I²C cycles. A
  latched power-board fault retransmits the complete Stop sequence every 500 ms.
- Start timeout, unknown power status, active `R26` after Stop, or a temperature
  guard also causes Stop/fault.
- Hard run limit — 5 h. После reset нагрев не восстанавливается.
- High-priority power-control task зарегистрирован в 5-s Task Watchdog с panic/reset;
  после reset boot снова начинается с Stop.
- Вентилятором интерфейсная ESP32 не управляет.
- Fault handlers never write NVS. Only `settings.c` persists explicit
  settings/profile/Wi-Fi/admin changes; the application never erases NVS
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

- Setpoint range is `40…190 °C`. Retained-session active zero and steady holding have
  passed supervised checks. The adaptive braking, Pause recomputation, and topology
  crossing included in deployed `0.2.14-dev` still need complete supervised cookware
  characterization.
- Production keeps the 80 °C interface IGBT guard and a separate 210 °C bottom
  emergency cutoff. The power MCU's native E05 remains active.
- Полный перебор редких fault paths и длительный web/network soak могут быть
  выполнены позднее как необязательная характеризация.
