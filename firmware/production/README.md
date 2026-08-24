# MCL02M custom firmware

Рабочий проект новой интерфейсной прошивки для ESP-WROOM-32D в Xiaomi MCL02M.
Проект не содержит штатные Mi Home/MIoT, рецепты и NFC. В OLED-интерфейс
включены десять согласованных пользовательских полноэкранных 1-bit картинок и
шесть утверждённых PWM-мелодий.

## Реализовано

- подтверждённый heartbeat и whitelist силовой I²C (`0x20…0x2f`, записи только
  `0x0d/0x00/0x0c`);
- POWER `0…99`, TEMPERATURE с PREHEAT/APPROACH/HOLD и пределом HOLD `35`;
- Stop, Pause/Resume, NoPan `60 s` с обязательным циклом `мелодия → пауза 3 s`
  даже при `SOUND OFF`, critical fault latch, IGBT/bottom guards;
- cooking timer `MM:SS + HH` до 5 h, подтверждаемое отключение, RAM-last-value,
  COMPLETE melody;
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
  нулевая длительность пропускает ячейку, общий профиль не длиннее 5 h, web только
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

- Диапазон температуры `40…175 °C`, текущие PI coefficients и поведение
  `HOLD SATURATED` приняты владельцем после практических supervised-тестов;
  отдельная калибровка не планируется. Пределы `80/120 °C` остаются
  дополнительными interface guards поверх защиты силовой платы.
- GPIO32 не имеет подтверждённого эффекта и всегда оставлен LOW.
- Ventilator полностью принадлежит силовой плате, custom ESP им не управляет.
- HTTP предназначен для доверенной локальной сети и не содержит управления
  нагревом: нет удалённых Start/Stop/Pause/setpoint/timer/delayed Start.
- Сборка не является разрешением на прошивку или нагрев.

## Сборка без устройства

```powershell
idf.py set-target esp32
idf.py build
python tests/safety_check.py
python tests/localization_check.py
```

Допустимый артефакт для будущего отдельного согласования — только app image
`build/mcl02m_custom.bin` для stock `ota_1` (`0x170000`). Нельзя автоматически
писать bootloader, partition table, otadata, NVS, PHY или assets.
