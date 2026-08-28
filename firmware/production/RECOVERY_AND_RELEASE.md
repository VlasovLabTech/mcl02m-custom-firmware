# Release and recovery gate

Этот документ описывает будущую процедуру, но **не разрешает запись сейчас**.

Current reference app artifact: `build/mcl02m_custom.bin`, 881872 bytes,
SHA-256 `911dd850765094c4dc9ae50334cf2f4d9754c18762b81ad28813943d30943d39`.
Чистая пересборка может иметь другой hash из-за compile metadata; для release
нужно сохранить новый manifest и заново пройти все gates.

## Перед первым power test

1. Проверить оригинальный полный dump:
   `16,777,216` bytes, SHA-256
   `e7d3ef41f6b5802558698589d5f3a6467d89e6838e8efa3bb040ffe4048bcc8e`.
2. Выполнить `python tests/safety_check.py` и сохранить SHA-256 app image.
3. Сохранить копию исходных `ota_1` и `otadata` из полного dump отдельно.
4. Получить отдельное подтверждение пользователя на каждую flash-write операцию.
5. Записывать только `build/mcl02m_custom.bin` в `0x170000`; общая команда
   `idf.py flash` запрещена, потому что она предлагает также bootloader,
   partition table, otadata и app по адресу `0x10000`.
6. Переключение `otadata` согласовывается отдельно и только подготовленной
   минимальной записью. Никакого erase всего flash.

## Completed one-time NVS refresh

The exceptional NVS refresh requested by the owner was completed on 2026-08-28,
immediately before flashing `0.2.5-dev` to stock `ota_1`. Only the NVS range
`0x9000..0xCFFF` was erased. This operation must not be repeated during ordinary
updates or automatically applied to another cooker; a new explicit owner request is
required. The firmware contains no automatic NVS erase.

## Возврат

- Сначала вернуть загрузку в штатный `ota_0` минимальной OTA-select записью.
- При необходимости восстановить исходные `ota_1`/`otadata` из проверенного
  полного dump.
- Полное восстановление 16 MiB допустимо только как отдельная аварийная операция
  после сверки адреса, файла и SHA-256.
- eFuse, Secure Boot, Flash Encryption и partition table не изменять.

## Ступени supervised validation

1. Boot without pan, no Start: I²C readings/UI/web only.
2. Pan with water, POWER gear 1, 10 s; verify Stop and telemetry.
3. Heat briefly, select POWER 0, then a positive gear. Confirm UART/Wi-Fi reports
   `ACTIVE_ZERO`, command `81/00/00`, and one resume without any deliberate Stop
   frame; listen for unexpected relay clicks.
4. Pause/Resume and Cancel. Confirm Pause sends `81/00/00`, Resume does not send
   `00/00/00`, the cooking timer freezes, and Cancel does send full Stop.
5. NoPan remove/return well inside 60 s; countdown must freeze.
6. Boundary commands 35↔36 and 55↔56 no faster than stock 500-ms heartbeat.
7. Timer complete and completion melody.
8. Fault screen/alarm test without artificial overheating.
9. Only then short TEMPERATURE tests, starting at low target; HOLD must never
   exceed 35 and `HOLD SATURATED` must not raise it.
10. Во время обычных supervised-тестов проверять правдоподобность NTC/IGBT
   readings; не доводить плиту намеренно до IGBT interface guard 80 °C или
   штатной E05 силовой платы.
11. Проверить, что web-страница позволяет редактировать Presets, including a
    timed POWER-0 wait stage, но не содержит
    и не принимает Start/Stop/Pause/setpoint/timer/delayed Start; reset/power
    loss должен забывать schedule и не восстанавливать нагрев.

Каждая ступень требует присутствия пользователя и отдельного согласования на
нагрев. Вентилятор интерфейсной ESP не управляется и не тестируется командами.
