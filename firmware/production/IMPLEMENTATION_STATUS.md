# MCL02M custom firmware — implementation status

Дата: 2026-08-30
Версия исходников: `0.2.29-dev`
Статус: the hash-verified `0.2.28-dev-private` app was written only to stock
`ota_1` at `0x170000` on the development unit on 2026-08-30; supervised
sound/localization confirmation remains pending. Supervised testing of `0.2.24-dev` confirmed retained-session active zero without unwanted relay switching,
Sleep/Wake, the temporary I2C debug display, and temperature operation. At a 125 °C
empty-pan setpoint, initial heating overshot by approximately
5 °C and subsequent holding was accurate. The source includes adaptive initial
braking, pause-safe output recomputation, and direct low/high topology crossing. Its
physical Settings menu exposes both the compile-time firmware version and live raw
power-board `R28` on a dedicated four-line, left-aligned OLED screen. The
`0.2.15-dev` source excludes the temporary I2C-loss counter from the
production menu and OLED while preserving that implementation behind a disabled
compile-time flag. Internal E09 counting and compact diagnostics are unchanged. The
`0.2.16-dev` source also replaces the queued timer toggle with synchronous
Set/Disable and uses a seconds, minutes, hours confirmation sequence. The
`0.2.17-dev` source completes the Start/EST evidence package: Start cannot
be confirmed by stale or deadline-late `R26`, and an EST preserves the exact first
cause in RAM, compact UART and authenticated status. The deterministic host model
covers the complete known Start response matrix. Version `0.2.18-dev` implements
the transactional Stop package: all Stop origins converge on `STOPPING`, complete
zero commands continue until two fresh `R26=00` samples, and late recovery remains
possible after timeout or I²C loss without losing the first recorded cause. The
`0.2.19-dev` source adds a generation-tagged three-second cooking lease.
Only the cooking task renews it in live sessions; the independent power task expires
it into transactional Stop and retains a distinct `ECL / COOK LEASE` diagnosis. The
`0.2.20-dev` source implements confirmed Start, active zero, Pause and
Resume transactions. Requested, transmitted, feedback-observed and confirmed state
are distinct; a transition completes only after the matching command and a later
fresh compatible `R20/R26` sample, with generation checks, bounded deadlines and
exact rejection evidence. Active-zero confirmation remains an explicitly documented
inference because the power board does not report the selected gear. Version
`0.2.21-dev` adds a two-phase cookware-return transaction: recognized pan-present
feedback first confirms active zero, then the cooking layer refreshes temperatures,
resets PI/trend/saturation context, applies the small-cookware cap, recomputes output
and requests a separately confirmed Resume. Unknown `R20` values cannot prove return;
Stop and Pause replace or cancel the return generation. The
`0.2.23-dev` source separates the five-hour user/profile countdown from
the eight-hour retained-session wall bound, two-hour manual Pause and three-second
cooking lease, and exposes distinct heating, ordinary active-zero, profile POWER-0,
Pause and NoPan elapsed counters. Delayed expiry synchronizes the physical UI to the
actual selected mode from any menu. Profile selection cannot mutate a delayed run,
and long-center Stop/Cancel takes priority over an open timer editor. The
same `0.2.23-dev` batch separates required startup capabilities from best-effort
`R2C`–`R2F` service reads, emits compact boot/cooking evidence for supervised UART
capture, invalidates stale temperature-trend samples after a sensor-data gap, defers
profile-cell completion across pending power transactions, and clears Pause-only
diagnostic state on every terminal/non-Pause transition. Hardware validation then
reproduced a Delayed Start `ETM`: the deadline began Start after the loop had already
captured a stopped power-board snapshot. Version `0.2.24-dev` refreshes that snapshot
after a successful scheduled Start, ignores stopped feedback while the Start
transaction is still pending, and orders the waiting OLED as mode, selected value,
delay label, countdown. Targeted supervised tests confirmed delayed POWER and
TEMPERATURE Starts reached `COOKING` without `ETM`, cancellation did not revive an
expired schedule, and temperature reached its setpoint and entered active zero. A
POWER 10 NoPan test forced zero output; cookware return confirmed `PAN_RETURN_HOLD`
before a separate `PAN_RETURN_RESUME` restored gear 10. Every user Stop completed
transactionally. The `0.2.25-dev` UI dismisses an existing timed picture
on the next physical press or encoder movement before processing that action, keeps
the large live screen during transactional Stop, and blocks `START AT` with localized
`TIME / NOT SET` when the wall clock is invalid. The
`0.2.26-dev` installs the revised 19-frame production artwork
pack. Successive completions rotate `ready1/2/3`; delayed Start uses `time` with a
compact countdown and `P`/`t`/`pr` badge; restricted cookware uses `small` with
`P<36` and a localized label; Wi-Fi connection uses `wifipresent`. A stopped,
idle cooker with a valid surface reading above 60 °C shows blinking `hot` after
five inactive seconds and cannot enter automatic or manual Sleep until it cools.
Completion Ready remains latched above Hot for at most one minute, then returns to
Idle so Hot/OLED/Sleep policy resumes even without acknowledgement. `noopls`, `toohot`
and `whatisgoingon` are compiled but not selected by any runtime path. Version
`0.2.27-dev` removes the exclamation mark from the cookware limit and adds the
previously missing `<` OLED glyph. The `0.2.28-dev` source replaces the repeating
NoPan warning with the complete
128-second Nutcracker table. Confirmed melody completion enters E02; separate 30-second
start and 132-second post-start watchdogs cover a failed sound task without truncating
a legitimately queued melody. Cookware return and Stop cancel only the NoPan
request, so a later removal starts from the first note. In current `0.2.29-dev`,
short center, long center, and Cancel all converge on the same idempotent Stop;
NoPan is rejected as a Pause source in both control layers, eliminating the
unconfirmed Pause transition that could surface as `EPB`. Protected
Wake/Sleep/NoPan/critical requests discard ordinary queued clicks. The default public
build and opt-in ignored private-sound build compile identical control and safety
sources; the private flavor substitutes only Wake and Sleep tables and has a distinct
project name and build directory. The unflashed `0.2.29-dev` source localizes the
remaining Chinese firmware/version,
power-board and Stop labels, localizes the unknown-`R20` warning in all three OLED
languages, adds its required CJK glyphs, and updates the trilingual user manual. The
earlier one-time NVS refresh is complete and must not be repeated automatically.

## Реализованный пользовательский контур

| Блок | Поведение |
|---|---|
| POWER | `0…99`; encoder slow `1`, fast `5`; вращение не запускает нагрев; gear 0 enters the active-zero session rather than full Stop |
| SMALL COOKWARE | `R26=01` is accepted as normal heating, not a fault; every control mode is capped at real gear `35` and forced to `A1`; POWER shows the permitted value, rejects upward edits above 35 with a 3-s `small` picture carrying `P<36` and localized `SMALL/МАЛ/小锅`, and still allows downward edits; `R26=02` clears the restriction |
| VERSION / BOARD | the physical firmware screen has four left-aligned rows: firmware label/version and live raw power-board `R28 XX`; invalid startup data is shown as `R28 --` |
| UNKNOWN R20 | `2B`, `29`, and `2A` remain silent nonfaults; another unrecognized nonzero value shows a persistent `WARNING / UNKNOWN / R20 XX`; the first physical input dismisses only the warning and the established session continues |
| TEMPERATURE | `40…190 °C`; entering T°C immediately copies and clamps the setpoint into editor-owned state; PREHEAT uses `56/77/99`; braking reserve is `clamp(10 °C + positive four-second rise, 10…20 °C)`, with a 15 °C minimum from 170 °C and 5 °C phase hysteresis; APPROACH uses `8 + 2 × error`; PI uses `4 + 2 × error + 0.08 × integral`; APPROACH/HOLD remain capped at `35`; output is active zero at/above target and PI resumes one degree below |
| HOLD SATURATED | gear `35` в течение 90 s при ошибке не менее 3 °C: orange, warning и сообщение; предел не повышается |
| Start | only a separate center press; power preflight and generation-tagged `STARTING` remain pending until fresh compatible `R20` plus `R26=01/02` arrive after the matching successful heartbeat and strictly before the sole 8-s deadline; EST latches repeated Stop plus immutable first-cause evidence |
| Pause/Resume | short center requests a generation-tagged transition and repeated presses remain idempotent until confirmation; Pause is confirmed from fresh retained-session feedback before its timer/state change; temperature trend sampling continues while PI is frozen; Resume recomputes and stages current output before its matching 3-s confirmation window, without Stop/re-arm; 2 h continuous confirmed manual Pause performs full Stop |
| Stop/Sleep | hold центра 1,5 s с немедленным срабатыванием и звуком — Stop/Back; transactional STOPPING retains the clean large live screen instead of technical text; следующий hold в Idle — Sleep unless a valid surface reading remains above 60 °C; Cancel всегда Stop/Back |
| Sleep | default 1 min Idle; automatic and manual Sleep are blocked above a valid 60 °C surface reading; wake returns to Home/Power, or Home/Temperature when Sleep began from Temperature; secondary menu selections are never restored; heartbeat and safety continue at zero output |
| SOUND FLAVORS | public `mcl02m_custom` includes the 128 s Nutcracker NoPan table and public Wake/Sleep; private `mcl02m_custom_private` is opt-in, uses the ignored local Wake/Sleep tables, and is built only in ignored `build_private`; all non-sound sources are shared |
| OLED | renderer использует полные `64×48` без отладочной рамки; 19 compiled frames include updated turn-on/wake/cooking/confirm/cancel/no-pan/error/Sleep artwork, three Ready variants rotated per completion, Wi-Fi, Hot, Time and Small plus three reserved inactive frames; Ready auto-closes after one minute, then Hot/OLED/Sleep policy resumes; Hot appears after five inactive seconds above 60 °C and blinks 2 s on / 1 s blank; a physical press/turn dismisses an existing timed picture before its own action; active timeout default 3 min; первое нажатие/вращение только будит; encoder guard 1,5 s |
| TIMER SCREEN | `AUTO` полностью гасит OLED; `ALWAYS` оставляет только countdown и перемещает его раз в минуту |
| Cooking timer | large `MM:SS` editor confirms blinking seconds first and blinking minutes second, followed by a separate hours confirmation; maximum 5 h; Set and Disable complete synchronously, an all-zero value is rejected, and rapid disable/re-enable cannot race queued toggles; countdown freezes only in manual Pause and NoPan; at ≥1 h the running screen shows `H:MM′`, below one hour `MM:SS″`; RAM only |
| Session timing | retained power session has a separate 8 h wall guard; manual Pause has its own 2 h limit; authenticated status separately reports actual heating, ordinary active zero, profile POWER-0 wait, Pause and NoPan elapsed time plus the independent 3 s cooking lease |
| Delayed start | крупные `START IN/START AT` (`СТАРТ ЧЕРЕЗ/СТАРТ В`) `HH:MM`; максимум ближайшие 24 h; invalid clock blocks `START AT` with `TIME / NOT SET`; while armed, the Time picture shows a compact countdown and `P85`/`t107`/`pr2`-style badge; Cancel или hold центра отменяют before the deadline update; RAM only and never restored after power loss; timer LED мигает; expiry from any menu synchronizes to POWER/TEMPERATURE/PROFILE; profile selection is immutable during delay; NoPan uses the same 128 s completion-driven policy as a manual start |
| Clock | отдельный пункт `CLOCK/ЧАСЫ` показывает идущие `HH:MM`; ручные 24-часовые `HH:MM` без секунд и без flash-write; реальная SNTP-синхронизация всегда приоритетнее ручной установки; после полного снятия питания offline clock снова недействителен |
| Profiles | five NVS profiles with up to five timed POWER/TEMPERATURE cells; POWER gear 0 is an active-zero wait cell and is not subject to the manual-Pause timeout; zero duration skips a cell; total maximum 5 h; Load still requires a separate physical Start |
| Readings | INFO: три строки `VOLT`, `NTC`, `IGBT`; сверху рабочего POWER — NTC, сверху TEMPERATURE — `S…°` и power; без timer опциональный IGBT заменяет context на 2 s после 5 s context; с timer IGBT скрыт, context остаётся; Pause скрывает timer/context |
| Languages | English / Русский / `简体中文`; китайский использует встроенный 8×8 subset из фактически нужных глифов, UTF-8 decoder поддерживает трёхбайтовые code points, все текстовые строки статичны и проходят 64-px width/coverage gate; полноэкранные картинки не содержат языкового текста |
| Sounds | Утверждённые PWM-таблицы: boot, wake, complete, NoPan, critical и sleep; normal duty 50%, sleep около 18%; Main, Timer и Cancel сохраняют UI click, encoder беззвучен, STAGE/WARNING остаются короткими; `SOUND OFF` не отключает NoPan и critical |
| Critical | Stop + latched `error.png` с фактическим E-кодом; один мотив 2,5 s проигрывается дважды на burst, всего 3 bursts с паузами 4 s; ACK немедленно вызывает `sound_stop()`; Sleep запрещён до ACK |
| NoPan | три последовательных отсчёта по 500 ms; отдельный `nopan.png`, orange blink, полная обязательная 128 s Nutcracker melody even with `SOUND OFF`, timer freeze, then E02 after confirmed playback completion; separate 30 s start and 132 s post-start watchdogs cover sound-task failure without consuming playback time while protected Wake/Sleep finishes; recognized return cancels the melody, first confirms active zero, then refreshes readings and recomputes POWER/TEMPERATURE/profile output under a new generation; another removal restarts the melody from its first note; unknown R20 cannot prove return; short/long center and Cancel immediately cancel the melody and converge on the same idempotent transactional Stop; NoPan cannot enter Pause |
| Stock E-groups | известные устойчивые raw-группы сохраняют E03/E04/E05/E07/E08/E10/E12; communication — E09; неизвестные остаются generic |
| I²C debug | The internal consecutive-bad-cycle counter, E09 threshold and diagnostic transport remain active. The temporary physical setting and OLED `0…6` overlay are compiled out of the production build with `COOKER_I2C_DEBUG_DISPLAY_ENABLED=0`; their source and two-second peak-hold implementation remain available for a future diagnostic build |
| Active-zero debug | Compile-time removable compact UART frames retain full state, `R20…R27`, last `0D/00/0C`, temperatures, faults and counters, plus input and Pause/Resume decisions; authenticated Wi-Fi keeps the readable JSON snapshot |

## Next planned work

The remaining product-facing work is intentionally limited to melody refinement,
mapping or revising the reserved OLED artwork, and profile UX/behavior. The two-hour manual-Pause duration
and 190 °C limit tests remain deferred rather than simulated.

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
