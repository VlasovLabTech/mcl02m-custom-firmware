В `assets/sounds/final`
лежит утверждённый набор из 6 PWM-мелодий. Прочитай `README.md` и интегрируй
`melody_tables.h` в `firmware/production`: табличный проигрыватель нот, новый
`SOUND_WAKE` только для перехода sleep→awake, sleep duty около 18%, остальные
50%. Сохрани текущие `UI_CLICK/STAGE/WARNING`, mandatory NoPan и critical,
interruptible queue и critical envelope: два 2.5-секундных мотива на burst,
три burst с паузами 4 s, ACK немедленно вызывает `sound_stop()`. Выполни build,
safety/policy tests и image validation. Устройство не прошивай.
