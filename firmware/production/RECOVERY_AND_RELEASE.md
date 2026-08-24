# Release and recovery gate

Этот документ описывает будущую процедуру, но **не разрешает запись сейчас**.

Текущий reference app artifact: `build/mcl02m_custom.bin`, 877488 bytes,
SHA-256 `045d50e56bb3c537b95a93df1bf9f2aa2b2762464b3c1d774a0d7a6e066a1bd4`.
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
3. Pause/Resume and Cancel; verify actual power board state.
4. NoPan remove/return well inside 60 s; countdown must freeze.
5. Boundary commands 35↔36 and 55↔56 no faster than stock 500-ms heartbeat.
6. Timer complete and completion melody.
7. Fault screen/alarm test without artificial overheating.
8. Only then short TEMPERATURE tests, starting at low target; HOLD must never
   exceed 35 and `HOLD SATURATED` must not raise it.
9. Во время обычных supervised-тестов проверять правдоподобность NTC/IGBT
   readings; не доводить плиту намеренно до interface guards 80/120 °C.
10. Проверить, что web-страница позволяет редактировать Presets, но не содержит
    и не принимает Start/Stop/Pause/setpoint/timer/delayed Start; reset/power
    loss должен забывать schedule и не восстанавливать нагрев.

Каждая ступень требует присутствия пользователя и отдельного согласования на
нагрев. Вентилятор интерфейсной ESP не управляется и не тестируется командами.
