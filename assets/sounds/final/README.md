# Final MCL02M PWM sound pack

Утверждённый набор из шести монофонических мелодий для пассивной пищалки
GPIO23. WAV используются только для прослушивания на компьютере; ESP32 должна
воспроизводить таблицы нот из `melody_tables.h` через PWM.

| Runtime event | Таблица | Мелодия | Длительность | Duty |
|---|---|---|---:|---:|
| включение | `k_sound_boot` | «Коробейники» | 4.80 s | 50% |
| готовка завершена | `k_sound_complete` | `When the Saints` | 4.29 s | 50% |
| пропажа посуды | `k_sound_midi_nutcracker_pas_de_deux` | Nutcracker, Pas de Deux | 128.00 s | 50% |
| критическая ошибка | `k_sound_critical` | Пятая симфония Бетховена | 2.50 s | 50% |
| переход в сон | `k_sound_sleep` | низкая `Twinkle Twinkle` | 4.94 s | около 18% |
| пробуждение | `k_sound_wake` | простой восходящий арпеджио | 1.85 s | 50% |

## Интеграция

1. Подключить `melody_tables.h` к `firmware/production/main/sound.c` и заменить
   hardcoded-последовательности выбранных событий единым проигрывателем таблиц
   `{frequency_hz, on_ms, gap_ms}`. `UI_CLICK`, `STAGE` и общий `WARNING` можно
   оставить текущими короткими паттернами.
2. `SOUND_NO_PAN` plays the complete Nutcracker table exactly once. E02 follows
   confirmed playback completion; cookware return, Pause, or Stop cancels only that
   request, and a later removal restarts it from the first note.
3. Для настоящего тихого sleep оставить совместимый 50%-wrapper
   `ui_buzzer_chirp()` и добавить отдельный API с duty; использовать около 18%
   только для `k_sound_sleep`.
4. Критическая мелодия — мотив 2.5 s. В существующем safety-envelope играть два
   мотива на каждый примерно 5-секундный burst: всего три burst с паузами 4 s.
   Critical and NoPan remain mandatory when normal sound is disabled; ACK должен
   немедленно прерывать очередь через `sound_stop()`.
5. Wake, Sleep, NoPan, and critical melodies are protected from ordinary queued
   clicks. The opt-in private flavor obtains only its Wake/Sleep tables from an
   ignored local header; the public build cannot include those symbols.
6. Сохранить interruptible waits, queue semantics и лимит одной ноты 1000 ms.
   После изменения выполнить ESP-IDF build, policy/safety tests и image
   validation. Устройство не прошивать без отдельного подтверждения владельца.

## Содержимое

- `melody_tables.h` — применяемые PWM-таблицы и рекомендуемые duty;
- `previews/00_selected_pack_in_order.wav` — весь набор по порядку;
- `previews/01...06_*.wav` — отдельные компьютерные превью.
