# MCL02M custom firmware

Рабочий проект новой интерфейсной прошивки для ESP-WROOM-32D в Xiaomi MCL02M.
Проект не содержит штатные Mi Home/MIoT, рецепты и NFC. В OLED-интерфейс
включены десять согласованных пользовательских полноэкранных 1-bit картинок и
шесть утверждённых PWM-мелодий.

## Реализовано

- подтверждённый heartbeat и whitelist силовой I²C (`0x20…0x2f`, записи только
  `0x0d/0x00/0x0c`);
- POWER `0…99`, TEMPERATURE с PREHEAT/APPROACH/HOLD и пределом HOLD `35`;
- `R26=01` restricted-cookware feedback is accepted as normal heating and caps every
  control path at real gear `35`/`A1`; POWER reports the permitted value, blocks only
  upward edits above 35, and displays a temporary explanatory message;
- physical Settings shows both `0.2.22-dev` firmware and live raw `R28` power-board
  revision/type with four left-aligned rows;
- `R20=2B/29/2A` are silent nonfaults; another unknown nonzero `R20` shows its exact
  hex value as a persistent warning, and the first physical input dismisses only the
  warning without changing the cooking state;
- active zero `0x81/0/0` for POWER 0, temperature coast and Pause; manual Pause
  performs a full Stop after 2 h while ordinary zero-power sessions continue;
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
- Stop, Pause/Resume, NoPan `60 s` с обязательным циклом `мелодия → пауза 3 s`
  даже при `SOUND OFF`; cookware return first confirms active zero, then refreshes
  readings and confirms a separately recomputed output; critical fault latch and
  IGBT/bottom guards remain active;
- cooking timer up to 5 h with sequential `SECONDS → MINUTES → HOURS` confirmation,
  synchronous explicit Set/Disable, RAM-last-value, and COMPLETE melody;
- separate accounting for the five-hour countdown, eight-hour retained-session wall
  guard, actual heating, ordinary active zero, profile POWER-0 waits, manual Pause,
  NoPan and the independent cooking lease;
- Delayed Start synchronizes the panel to POWER, TEMPERATURE or the selected PROFILE
  after expiry; profile selection remains immutable during the delay, and long-center
  Stop/Cancel outranks an open timer editor;
- `START IN` и `START AT`, только после физического задания/разрешения;
- OLED-off/wake guard, 9 LED мощности, timer LED, blue/orange status, buzzer;
- полноэкранные `64×48` turn-on/wake/cooking/confirm/cancel/ready/no-pan/error/
  sleep-warning/sleep картинки; error-шаблон получает фактический E-код;
- табличные PWM-мелодии boot/wake/complete/NoPan/critical/sleep; sleep работает
  с duty около 18%, остальные — 50%; UI click, STAGE и WARNING сохранены;
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
- Automatic low/high ramps cross directly between gears 35 and 56 without transiently
  commanding the middle topology. Manual POWER gears 36…55 remain available.
- Production keeps the interface-side 80 °C IGBT guard and uses a separate
  210 °C bottom emergency cutoff. The power MCU's native E05 remains active.
- Interface-generated E09 now requires six consecutive bad 500-ms I²C cycles.
  While any power-board fault remains latched, the complete Stop sequence is
  retransmitted every heartbeat.
- The production build omits the temporary `Settings → Show → I2C Errors` item and
  OLED overlay. Their implementation, stored field, and two-second displayed-peak
  hold remain in the source behind `COOKER_I2C_DEBUG_DISPLAY_ENABLED=0`; changing the
  flag creates a future diagnostic build. The internal counter, six-cycle E09 policy,
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
  The development unit's all-channel LED outage is recorded as a pre-existing
  hardware problem, not a regression caused by the current firmware changes.
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

Допустимый артефакт для будущего отдельного согласования — только app image
`build/mcl02m_custom.bin` для stock `ota_1` (`0x170000`). Нельзя автоматически
писать bootloader, partition table, otadata, NVS, PHY или assets.
