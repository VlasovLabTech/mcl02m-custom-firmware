# MCL02M custom firmware

Рабочий проект новой интерфейсной прошивки для ESP-WROOM-32D в Xiaomi MCL02M.
Проект не содержит штатные Mi Home/MIoT, рецепты и NFC. В OLED-интерфейс
включены 19 согласованных пользовательских полноэкранных 1-bit картинок, включая
три пока неактивных резервных кадра, и шесть утверждённых PWM-мелодий.

## Реализовано

- подтверждённый heartbeat и whitelist силовой I²C (`0x20…0x2f`, записи только
  `0x0d/0x00/0x0c`);
- POWER `0…99`, TEMPERATURE с PREHEAT/APPROACH/HOLD и пределом HOLD `35`;
- `R26=01` restricted-cookware feedback is accepted as normal heating and caps every
  control path at real gear `35`/`A1`; POWER reports the permitted value, blocks only
  upward edits above 35, and displays a temporary explanatory message;
- physical Settings shows both `0.2.33-dev` firmware and live raw `R28` power-board
  revision/type with four left-aligned rows;
- `R20=2B/29/2A` are silent nonfaults; another unknown nonzero `R20` shows its exact
  hex value as a persistent warning, and the first physical input dismisses only the
  warning without changing the cooking state;
- active zero `0x81/0/0` for POWER 0, temperature coast and Pause; manual Pause
  performs a full Stop after 2 h while ordinary zero-power sessions continue;
- native E07 still requires two matching `R20=17` samples. During an active session,
  two valid IGBT readings above 92 °C produce a persistent warning with three short
  beeps every three seconds and no power reduction. Physical input hides only its
  screen for seven seconds; it remains active at 92 °C and clears below 92 °C. Two
  valid readings above 98 °C Stop with a separately marked interface E07, while a
  new Start is blocked above 80 °C;
- every normal and safety Stop enters one idempotent `STOPPING` transaction, repeats
  `00/00/00` until two fresh `R26=00` samples, and exposes preserved timeout/I²C
  evidence without falsely announcing `IDLE` or `COMPLETE`;
- a generation-tagged 3-s cooking lease is renewed only by the cooking task in live
  sessions; the independent 500-ms power task converts expiry into transactional Stop
  and preserves the distinct `ECL / COOK LEASE` cause;
- Start, active zero, Pause and Resume are generation-tagged pending transactions;
  only a matching successful command followed by fresh compatible `R20/R26` feedback
  confirms them, repeated short presses are idempotent, and exact timeout/rejection
  reasons remain available in compact UART and authenticated status;
- Stop, Pause/Resume, and completion-driven NoPan: the complete 128 s Nutcracker
  melody plays once even with `SOUND OFF`, then E02 is raised; separate 30 s start
  and 132 s post-start watchdogs cover a failed sound task without consuming the
  melody budget while a protected Wake/Sleep finishes. Cookware return first cancels only that melody and confirms active zero, then refreshes
  readings and confirms a separately recomputed output; critical fault latch and
  IGBT/bottom guards remain active;
- cooking timer up to 5 h with sequential `SECONDS → MINUTES → HOURS` confirmation,
  synchronous explicit Set/Disable, RAM-last-value, and COMPLETE melody;
- separate accounting for the five-hour countdown, eight-hour retained-session wall
  guard, actual heating, ordinary active zero, profile POWER-0 waits, manual Pause,
  NoPan and the independent cooking lease;
- Delayed Start synchronizes the panel to POWER, TEMPERATURE or the selected PROFILE
  after expiry, refreshes the post-Start power-board snapshot before classifying
  feedback, and keeps a pending Start from misreading the preceding stopped state as
  `ETM`; the waiting screen uses the dedicated Time picture with compact countdown
  and `P`/`t`/`pr` mode badge;
  profile selection remains immutable during the delay, and long-center Stop/Cancel
  outranks an open timer editor;
- `START IN` и `START AT`, только после физического задания/разрешения;
- an invalid wall clock blocks `START AT` and shows localized `TIME / NOT SET`
  instead of silently opening an editor or accepting a schedule;
- OLED-off/wake guard, 9 LED мощности, timer LED, blue/orange status, buzzer;
- полноэкранные `64×48` turn-on/wake/cooking/confirm/cancel/no-pan/error/
  sleep-warning/sleep картинки; three Ready frames rotate per completion, and the
  error template receives the live E-code;
- successful Wi-Fi connection and entry to an already-connected Wi-Fi menu show the
  Wi-Fi picture; small cookware shows a 3-s picture with `P<36` and localized label;
- after five inactive seconds above a valid 60 °C surface reading, the Hot picture
  blinks 2 s on / 1 s blank; completion Ready has priority for at most one minute,
  then returns to Idle so Hot/OLED/Sleep policy resumes; automatic or manual Sleep
  remains blocked until cooling;
- delayed Start continuously shows the Time picture with countdown and mode badge;
  `noopls`, `toohot` and `whatisgoingon` are compiled but have no trigger yet;
- any new physical press or encoder movement dismisses an existing timed picture;
  the action is still processed and may intentionally create its own new picture;
- transactional Stop retains the clean large live screen and never exposes the
  internal five-line `STOPPING` status page;
- NoPan treats center short, center long, and Cancel as the same idempotent Stop;
  the warning melody is cancelled immediately and a NoPan Pause transition is
  rejected at both the cooking-engine and power-board-control layers;
- table-driven PWM melodies for boot/wake/complete/NoPan/critical/sleep. The public
  build contains the full public-domain Nutcracker NoPan table and the existing
  public Wake/Sleep tables. The opt-in private build substitutes only local Wake and
  Sleep tables. Long Wake/Sleep/NoPan playback drops ordinary queued clicks; Cancel
  or forced long-hold Sleep can cancel Wake, and cookware return/Pause/Stop cancels
  only NoPan;
- OLED timeout default 3 min с фиксированными интервалами до 5 h; активный экран
  показывает timer и контекстный NTC/gear, опционально IGBT;
- Settings в versioned NVS namespace без автоматического erase; отдельный
  физически подтверждаемый Factory reset очищает только namespace custom firmware;
- Wi-Fi по умолчанию выключен, включается из физического `Setup → Wi-Fi`,
  сохраняет toggle и STA credentials; AP+STA provisioning, NTP и web login/session/CSRF;
- пять сохраняемых Profiles по пять последовательных POWER/TEMPERATURE-ячеек;
  POWER 0 is a timed active-zero wait stage, нулевая длительность пропускает
  ячейку, общий профиль не длиннее 5 h, web только
  редактирует, а выбор и Start выполняются с физической панели.
- три языка OLED-интерфейса: English, Русский и упрощённый китайский `简体中文`;
  китайские надписи используют компактный встроенный 8×8 subset, все строки
  статичны и проверяются на ширину 64 px.

Подробности:

- [Implementation status](IMPLEMENTATION_STATUS.md)
- [Local web API](WEB_API.md)
- [Build manifest](BUILD_MANIFEST.md)
- [Release and recovery gate](RECOVERY_AND_RELEASE.md)

## Важные ограничения dev-версии

- Temperature setpoints are `40…190 °C`. Starting above the setpoint enters active
  zero. At or above the target the output remains zero; at the first whole degree
  below target the ordinary PI controller resumes. Initial heating uses a four-second
  trend: braking reserve is `clamp(10 °C + positive rise, 10…20 °C)`, with a 15 °C
  minimum for targets of 170 °C and above and five-degree phase hysteresis. APPROACH
  and HOLD remain capped at gear 35.
- Temperature Pause keeps observing the NTC trend but freezes PI. Resume computes and
  installs a fresh output before reactivating the retained power-board session.
- Cold Start chooses the target's final topology before applying output: `1…10`
  direct, `11…35` from 10, `36` direct, `37…55` from 36, `56` direct, and
  `57…99` from 56. Ten-level heartbeat steps then remain inside that topology,
  eliminating artificial cold-Start relay/IGBT transitions. A live low/high target
  change still crosses directly at 35↔56; manual POWER gears 36…55 remain available.
- Production has a persistent active-session IGBT advisory after two readings above
  92 °C and a separately marked interface E07 after two readings above 98 °C. Start
  is blocked above 80 °C. Raw sensor faults require two samples; the separate bottom
  emergency cutoff requires six consecutive readings strictly above 210 °C. The
  power MCU's native E05/E07 protections remain active.
- Interface-generated E09 classifies `R20/R22/R23/R24/R26` and all control writes as
  critical, while `R21/R25/R27` are service-only and cannot cause E09 alone. Three
  critical-bad cycles enter a 320-ms critical-only recovery poll; two good critical
  cycles return to the normal 500-ms schedule. Continuous critical loss faults after
  5 s, or failed control writes after 3 s. A latched fault keeps retransmitting Stop.
- The production build omits the temporary `Settings → Show → I2C Errors` item and
  OLED overlay. Their implementation, stored field, and two-second displayed-peak
  hold remain in the source behind `COOKER_I2C_DEBUG_DISPLAY_ENABLED=0`; changing the
  flag creates a future diagnostic build. The classified recovery state and counters,
  compact UART frame, and authenticated status data remain active.
- Active-zero diagnostics are available as a compact fixed UART frame and through
  authenticated `/api/status`. The UART frame retains the full driver state, last
  `0D/00/0C` command, raw `R20…R27`, temperatures, fault and counters without JSON
  key overhead. Short `Z/P/U/F/I` frames mark transitions and errors; `B/E/T/C`
  frames record button, encoder, touch and Pause/Resume decisions. Both
  active-zero switches are compile-time definitions for quick removal after
  supervised validation.
  The fixed `D` field order is: state, target/applied gear, topology, last commands,
  `R20…R27`, valid mask, temperatures, run/remaining/arm/start-confirm/heartbeat-gap
  timers, cycle/error/active-zero counters, stop/heartbeat/active-zero flags, fault.
- `0.2.23-dev` adds a one-shot `B` power-board capability frame and event-driven `C`
  cooking frames. Required startup reads have a separate validity mask; failures of
  best-effort `R2C`–`R2F` remain visible without producing `BOOT I2C`. The passive
  `tools/monitor_uart.py` recorder stores every raw line while summarizing changed
  `D` frames and events on screen.
- Start acknowledgement is accepted only after a complete successful nonzero
  heartbeat and strictly before its single eight-second deadline. EST captures one
  immutable first-cause RAM record before repeated Stop changes live feedback. The
  one-shot compact `X` frame and authenticated status include state, `R20/R26/R28`,
  valid mask, requested/transmitted command, cycle counters, timestamp and cause.
- Fault handling never writes runtime or LED state to NVS. Only explicit settings,
  profile, Wi-Fi, admin, and physical Factory actions use the custom namespace.
- All nine white power LEDs run a 1.5-second all-on boot test. Serial LED writes end
  with an explicit STB latch and changed white/orange/blue/timer requests emit one
  compact `L` frame. If the test remains invisible while the log requests nine LEDs,
  inspect the common panel-driver supply, connector, flex and STB/CLK/DATA path.
- GPIO32 не имеет подтверждённого эффекта и всегда оставлен LOW.
- Ventilator полностью принадлежит силовой плате, custom ESP им не управляет.
- HTTP предназначен для доверенной локальной сети и не содержит управления
  нагревом: нет удалённых Start/Stop/Pause/setpoint/timer/delayed Start.
- Сборка не является разрешением на прошивку или нагрев.

## Сборка без устройства

```powershell
idf.py set-target esp32
idf.py build
python tests/policy_tests.py
python tests/safety_check.py
python tests/localization_check.py
```

## Public and private sound builds

Both flavors compile the same control, safety, UI, and networking sources. The
public flavor is the default and has no include path to the ignored private MIDI
pack:

```powershell
idf.py -B build -D MCL02M_PRIVATE_SOUND_BUILD=OFF build
```

The private flavor is opt-in and fails closed when the local generated header is
absent:

```powershell
idf.py -B build_private -D MCL02M_PRIVATE_SOUND_BUILD=ON build
```

Its input is
`assets/sounds/midi/generated/code/melody_tables_midi.generated.h`. The input,
local generator workspace, `build_private/`, and all firmware binaries are ignored
by Git. The artifacts have distinct project names:

- public: `build/mcl02m_custom.bin`, app version `0.2.33-dev`;
- private: `build_private/mcl02m_custom_private.bin`, app version
  `0.2.33-dev-private`.

The physical version screen intentionally shows the shared source version
`0.2.33-dev`; `esptool image-info` exposes the private suffix. Never reuse one build
directory for both flavors.

Допустимый артефакт для будущего отдельного согласования — только app image
`build/mcl02m_custom.bin` для stock `ota_1` (`0x170000`). Нельзя автоматически
писать bootloader, partition table, otadata, NVS, PHY или assets.
