# Selected MCL02M sound pack

Historical selection snapshot. Production `0.2.28-dev` supersedes the NoPan row
with the complete 128-second Nutcracker table. The private Wake/Sleep MIDI inputs
and private build output are deliberately excluded from Git.

Пять подтверждённых владельцем мелодий и один новый кандидат для пробуждения:

| № | Событие | Выбранная основа | Длительность |
|---:|---|---|---:|
| 1 | включение | «Коробейники» из V3 | 4.80 s |
| 2 | завершение готовки | `When the Saints` из V4 | 4.29 s |
| 3 | пропажа посуды | sharp alarm из V4 | 2.74 s |
| 4 | критическая ошибка | Пятая симфония Бетховена из V3 | 2.50 s |
| 5 | сон | низкая `Twinkle Twinkle` из V4 | 4.94 s |
| 6 | пробуждение | новый простой восходящий арпеджио | 1.85 s |

`melody_tables_selected.generated.h` содержит финально именованные таблицы
`k_sound_boot`, `k_sound_complete`, `k_sound_no_pan`, `k_sound_critical`,
`k_sound_sleep` и `k_sound_wake`.

Production firmware пока не изменена. При будущей интеграции normal duty — 50%,
sleep — ориентировочно 18%; критический мотив должен сохранить существующее
обязательное повторение, работу при выключенном обычном звуке и немедленное
прерывание после подтверждения ошибки.

Пересборка:

```powershell
py -3 .\generate_selected_pack.py
```
