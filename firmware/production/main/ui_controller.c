#include "ui_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "cooking_engine.h"
#include "display_prod.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_prod.h"
#include "settings.h"
#include "sound.h"
#include "ui_inputs.h"

#define LINE_BYTES 32
#define HOME_ITEMS 7
#define SETTING_ITEMS 12
#define WIFI_ITEMS 4
#define EDITOR_TIMEOUT_US (10LL * 1000000LL)
#define TEMPERATURE_EDIT_TIMEOUT_US (2LL * 1000000LL)
#define BLINK_PERIOD_MS 1000U
#define BLINK_VISIBLE_MS 700U

typedef enum {
    VIEW_HOME = 0,
    VIEW_POWER,
    VIEW_TEMPERATURE,
    VIEW_READINGS,
    VIEW_SETTINGS,
    VIEW_SETTING_VALUE,
    VIEW_TIMER_MMSS,
    VIEW_TIMER_HOURS,
    VIEW_TIMER_DISABLE,
    VIEW_START_MENU,
    VIEW_START_IN_MINUTES,
    VIEW_START_IN_HOURS,
    VIEW_START_AT_HOURS,
    VIEW_START_AT_MINUTES,
    VIEW_PROFILES,
    VIEW_PROFILE_READY,
    VIEW_WIFI_MENU,
    VIEW_WIFI_STATUS,
    VIEW_WIFI_SETUP,
    VIEW_WIFI_PASSWORD,
    VIEW_FACTORY_CONFIRM,
    VIEW_CLOCK_HOURS,
    VIEW_CLOCK_MINUTES,
} view_t;

static volatile view_t s_view;
static unsigned s_selection;
static unsigned s_setting;
static unsigned s_start_selection;
static unsigned s_wifi_selection;
static int s_setting_value;
static uint32_t s_timer_mmss;
static uint32_t s_timer_hours;
static unsigned s_start_in_minutes = 10;
static unsigned s_start_in_hours;
static unsigned s_start_at_hour;
static unsigned s_start_at_minute;
static unsigned s_clock_hour;
static unsigned s_clock_minute;
static view_t s_clock_return_view;
static unsigned s_profile;
static int64_t s_last_input_us;
static int64_t s_last_encoder_us;
static int64_t s_encoder_guard_until_us;
static int64_t s_temperature_edit_deadline_us;
static unsigned s_fast_streak;
static bool s_swallow_main;
static bool s_swallow_timer;
static volatile bool s_timer_editing;
static view_t s_timer_return_view;
static unsigned s_wake_selection;

static void remember_primary_wake_selection(void)
{
    s_wake_selection = (s_view == VIEW_TEMPERATURE ||
                        (s_view == VIEW_HOME && s_selection == 1U)) ? 1U : 0U;
}

static void restore_primary_wake_selection(void)
{
    s_selection = s_wake_selection;
    s_view = VIEW_HOME;
}

static const char *tr(app_language_t language, const char *english,
                      const char *russian, const char *chinese)
{
    if (language == LANG_RU) return russian;
    if (language == LANG_ZH) return chinese;
    return english;
}

static const char *home_name(unsigned item, app_language_t language)
{
    static const char *en[HOME_ITEMS] = {
        "POWER", "T°C", "PRESET", "INFO", "START", "SETUP", "CLOCK"
    };
    static const char *ru[HOME_ITEMS] = {
        "МОЩН", "T°C", "ПРЕСЕТ", "ИНФО", "СТАРТ", "НАСТР", "ЧАСЫ"
    };
    static const char *zh[HOME_ITEMS] = {
        "功率", "温度", "预设", "信息", "启动", "设置", "时钟"
    };
    if (item >= HOME_ITEMS) return "?";
    if (language == LANG_RU) return ru[item];
    if (language == LANG_ZH) return zh[item];
    return en[item];
}

static const char *home_subtitle(unsigned item, app_language_t language)
{
    return item == 4 ? tr(language, "DELAYED", "ОТЛОЖЕННЫЙ", "延时") : "";
}

static const char *setting_name(unsigned item, app_language_t language)
{
    static const char *en[SETTING_ITEMS] = {
        "LANGUAGE", "SOUND", "SHOW", "SHOW", "SHOW", "SHOW",
        "SLEEP MIN", "OLED TIME", "TIMEZONE", "SHOW", "WI-FI", "FACTORY"
    };
    static const char *ru[SETTING_ITEMS] = {
        "ЯЗЫК", "ЗВУК", "ПОКАЗАТЬ", "ПОКАЗАТЬ", "ПОКАЗАТЬ", "ПОКАЗАТЬ",
        "СОН МИН", "ЭКРАН ВР", "ЧАС ПОЯС", "ПОКАЗАТЬ", "WI-FI", "ЗАВОДСКИЕ"
    };
    static const char *zh[SETTING_ITEMS] = {
        "语言", "声音", "显示", "显示", "显示", "显示",
        "休眠分钟", "屏幕时间", "时区", "显示", "WI-FI", "恢复出厂"
    };
    if (item >= SETTING_ITEMS) return "?";
    if (language == LANG_RU) return ru[item];
    if (language == LANG_ZH) return zh[item];
    return en[item];
}

static const char *setting_name_second(unsigned item, app_language_t language)
{
    switch (item) {
    case 2: return tr(language, "LIVE DATA", "РАБ ДАНН", "实时数据");
    case 3: return "IGBT T°C";
    case 4: return tr(language, "TIMER SCREEN", "ТАЙМЕР", "定时屏幕");
    case 5: return tr(language, "SLEEP CLOCK", "ЧАСЫ В СНЕ", "休眠时钟");
    case 9: return "I2C ERRORS";
    default: return "";
    }
}

static const uint16_t OLED_TIMEOUTS_S[] = {
    60, 120, 180, 300, 600, 1200, 1800, 3600, 7200, 10800, 14400, 18000,
};

static int oled_timeout_step(int current, int direction)
{
    unsigned index = 0;
    while (index + 1 < sizeof(OLED_TIMEOUTS_S) / sizeof(OLED_TIMEOUTS_S[0]) &&
           current > OLED_TIMEOUTS_S[index]) ++index;
    if (direction > 0 && index + 1 < sizeof(OLED_TIMEOUTS_S) / sizeof(OLED_TIMEOUTS_S[0]))
        ++index;
    else if (direction < 0 && index > 0)
        --index;
    return OLED_TIMEOUTS_S[index];
}

static void format_duration(uint32_t seconds, char output[16])
{
    const unsigned hours = seconds / 3600U;
    const unsigned minutes = (seconds / 60U) % 60U;
    const unsigned secs = seconds % 60U;
    if (hours) snprintf(output, 16, "%u:%02u:%02u", hours, minutes, secs);
    else snprintf(output, 16, "%02u:%02u", minutes, secs);
}

static void overlay(const char *a, const char *b, const char *c,
                    const char *d, const char *e)
{
    const char *lines[UI_OLED_TEXT_LINES] = {a, b, c, d, e};
    display_prod_set_overlay(lines);
}

static int encoder_step(const ui_input_event_t *event, bool timer)
{
    const int64_t now_us = event->timestamp_ms * 1000LL;
    const int64_t gap = s_last_encoder_us == 0 ? 1000000 : now_us - s_last_encoder_us;
    s_last_encoder_us = now_us;
    if (gap < 110000) ++s_fast_streak; else s_fast_streak = 0;
    int magnitude;
    if (timer) {
        magnitude = s_fast_streak >= 8 ? 60 : (s_fast_streak >= 4 ? 30 :
                    (s_fast_streak >= 2 ? 5 : 1));
    } else {
        magnitude = gap < 110000 ? 5 : 1;
    }
    /* The panel reports clockwise detents as negative; clockwise must increase. */
    return event->value < 0 ? magnitude : -magnitude;
}

static int clamp(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void render(void)
{
    cooker_snapshot_t status;
    app_settings_t settings;
    cooking_engine_get_snapshot(&status);
    settings_get(&settings);
    const app_language_t lang = settings.language;
    char l0[LINE_BYTES] = {0}, l1[LINE_BYTES] = {0}, l2[LINE_BYTES] = {0};
    char l3[LINE_BYTES] = {0}, l4[LINE_BYTES] = {0};
    const bool blink_on = ((xTaskGetTickCount() * portTICK_PERIOD_MS) % BLINK_PERIOD_MS) <
                          BLINK_VISIBLE_MS;
    const bool timer_active = status.timer_enabled;
    const bool show_igbt_phase = settings.show_igbt && !timer_active &&
                                 ((xTaskGetTickCount() * portTICK_PERIOD_MS) % 7000U) >= 5000U;

    switch (s_view) {
    case VIEW_HOME: {
        const char *subtitle = home_subtitle(s_selection, lang);
        if (s_selection == 6) {
            const time_t now = time(NULL);
            struct tm local = {0};
            if (now > 1700000000) {
                localtime_r(&now, &local);
                snprintf(l0, sizeof(l0), "%02d:%02d", local.tm_hour, local.tm_min);
            } else {
                strlcpy(l0, "--:--", sizeof(l0));
            }
            subtitle = l0;
        }
        display_prod_set_menu_overlay(s_selection + 1, home_name(s_selection, lang),
                                      subtitle);
        return;
    }
    case VIEW_POWER: {
        char right[LINE_BYTES] = {0};
        if (show_igbt_phase) {
            if (status.readings_valid) snprintf(right, sizeof(right), "I%u°", status.igbt_c);
            else strlcpy(right, "I--", sizeof(right));
        } else if (settings.show_context_value) {
            if (status.readings_valid) snprintf(right, sizeof(right), "N%u°", status.bottom_c);
            else strlcpy(right, "N--", sizeof(right));
        }
        snprintf(l1, sizeof(l1), "%u", status.selected_gear);
        display_prod_set_focus_overlay("", right, l1, false, "");
        return;
    }
    case VIEW_TEMPERATURE: {
        display_prod_set_temperature_edit_overlay(status.target_temperature_c,
                                                  status.bottom_c,
                                                  status.readings_valid);
        return;
    }
    case VIEW_READINGS:
        display_prod_set_info_overlay(status.mains_voltage_v, status.bottom_c,
                                      status.igbt_c, status.readings_valid);
        return;
    case VIEW_SETTINGS:
        snprintf(l0, sizeof(l0), "%s", tr(lang, "SETTINGS", "НАСТРОЙКИ", "设置"));
        snprintf(l1, sizeof(l1), ">%s", setting_name(s_setting, lang));
        snprintf(l2, sizeof(l2), "%s", setting_name_second(s_setting, lang));
        snprintf(l4, sizeof(l4), "%u/%u", s_setting + 1, SETTING_ITEMS);
        break;
    case VIEW_SETTING_VALUE:
        snprintf(l0, sizeof(l0), "%s", setting_name(s_setting, lang));
        snprintf(l1, sizeof(l1), "%s", setting_name_second(s_setting, lang));
        if (s_setting == 0) {
            static const char *language_names[] = {"ENGLISH", "РУССКИЙ", "中文"};
            snprintf(l2, sizeof(l2), "%s", language_names[clamp(s_setting_value, LANG_EN, LANG_ZH)]);
        }
        else if ((s_setting >= 1 && s_setting <= 5) || s_setting == 9)
            snprintf(l2, sizeof(l2), "%s", s_setting_value ?
                     tr(lang, "ON", "ВКЛ", "开") : tr(lang, "OFF", "ВЫКЛ", "关"));
        else if (s_setting == 8) {
            const int absolute = abs(s_setting_value);
            snprintf(l2, sizeof(l2), "UTC%c%d:%02d", s_setting_value >= 0 ? '+' : '-',
                     absolute / 60, absolute % 60);
        }
        else if (s_setting == 7) {
            if (s_setting_value < 3600)
                snprintf(l2, sizeof(l2), "%d %s", s_setting_value / 60,
                         tr(lang, "MIN", "МИН", "分钟"));
            else
                snprintf(l2, sizeof(l2), "%d %s", s_setting_value / 3600,
                         tr(lang, "H", "Ч", "小时"));
        } else snprintf(l2, sizeof(l2), "%d", s_setting_value);
        snprintf(l4, sizeof(l4), "%s", tr(lang, "PRESS SAVE", "НАЖ СОХР", "按键保存"));
        break;
    case VIEW_TIMER_MMSS: {
        char value[16];
        snprintf(value, sizeof(value), "%02lu:%02lu", (unsigned long)(s_timer_mmss / 60),
                 (unsigned long)(s_timer_mmss % 60));
        if (!blink_on) strlcpy(value, "  :  ", sizeof(value));
        display_prod_set_time_editor_overlay(tr(lang, "TIMER", "ТАЙМЕР", "定时器"), value,
                                             tr(lang, "MIN SEC", "МИН СЕК", "分 秒"));
        return;
    }
    case VIEW_TIMER_HOURS: {
        char value[4] = {0};
        if (blink_on) snprintf(value, sizeof(value), "%02lu", (unsigned long)s_timer_hours);
        else strlcpy(value, "  ", sizeof(value));
        display_prod_set_time_editor_overlay(tr(lang, "TIMER", "ТАЙМЕР", "定时器"), value,
                                             tr(lang, "HOURS", "ЧАСЫ", "小时"));
        return;
    }
    case VIEW_TIMER_DISABLE: {
        char remaining[16];
        format_duration(status.timer_remaining_s, remaining);
        snprintf(l0, sizeof(l0), "%s", tr(lang, "TIMER", "ТАЙМЕР", "定时器"));
        snprintf(l1, sizeof(l1), "%s", remaining);
        snprintf(l3, sizeof(l3), "%s", tr(lang, "DISABLE?", "ОТКЛЮЧИТЬ?", "关闭?"));
        snprintf(l4, sizeof(l4), "%s", tr(lang, "CENTER YES", "ЦЕНТР ДА", "中键确认"));
        break;
    }
    case VIEW_START_MENU:
        display_prod_set_menu_overlay(s_start_selection + 1,
                                      s_start_selection == 0 ?
                                      tr(lang, "IN", "ЧЕРЕЗ", "延后") :
                                      tr(lang, "AT", "В", "定时"),
                                      tr(lang, "START", "СТАРТ", "启动"));
        return;
    case VIEW_START_IN_MINUTES:
    case VIEW_START_IN_HOURS: {
        char hours[4], minutes[4];
        snprintf(hours, sizeof(hours), "%02u", s_start_in_hours);
        snprintf(minutes, sizeof(minutes), "%02u", s_start_in_minutes);
        if (!blink_on) {
            if (s_view == VIEW_START_IN_MINUTES) strlcpy(minutes, "  ", sizeof(minutes));
            else strlcpy(hours, "  ", sizeof(hours));
        }
        snprintf(l2, sizeof(l2), "%s:%s", hours, minutes);
        display_prod_set_time_editor_overlay(tr(lang, "START IN", "СТАРТ ЧЕРЕЗ", "延后启动"), l2,
                                             s_view == VIEW_START_IN_MINUTES ?
                                             tr(lang, "MINUTES", "МИНУТЫ", "分钟") :
                                             tr(lang, "HOURS", "ЧАСЫ", "小时"));
        return;
    }
    case VIEW_START_AT_HOURS:
    case VIEW_START_AT_MINUTES: {
        char hours[4], minutes[4];
        snprintf(hours, sizeof(hours), "%02u", s_start_at_hour);
        snprintf(minutes, sizeof(minutes), "%02u", s_start_at_minute);
        if (!blink_on) {
            if (s_view == VIEW_START_AT_HOURS) strlcpy(hours, "  ", sizeof(hours));
            else strlcpy(minutes, "  ", sizeof(minutes));
        }
        snprintf(l2, sizeof(l2), "%s:%s", hours, minutes);
        display_prod_set_time_editor_overlay(tr(lang, "START AT", "СТАРТ В", "定时启动"), l2,
                                             s_view == VIEW_START_AT_HOURS ?
                                             tr(lang, "HOURS", "ЧАСЫ", "小时") :
                                             tr(lang, "MINUTES", "МИНУТЫ", "分钟"));
        return;
    }
    case VIEW_CLOCK_HOURS:
    case VIEW_CLOCK_MINUTES: {
        char hours[4], minutes[4];
        snprintf(hours, sizeof(hours), "%02u", s_clock_hour);
        snprintf(minutes, sizeof(minutes), "%02u", s_clock_minute);
        if (!blink_on) {
            if (s_view == VIEW_CLOCK_HOURS) strlcpy(hours, "  ", sizeof(hours));
            else strlcpy(minutes, "  ", sizeof(minutes));
        }
        snprintf(l2, sizeof(l2), "%s:%s", hours, minutes);
        display_prod_set_time_editor_overlay(tr(lang, "CLOCK", "ЧАСЫ", "时钟"), l2,
                                             s_view == VIEW_CLOCK_HOURS ?
                                             tr(lang, "HOURS", "ЧАСЫ", "小时") :
                                             tr(lang, "MINUTES", "МИНУТЫ", "分钟"));
        return;
    }
    case VIEW_PROFILES: {
        cooker_profile_t profiles[COOKER_PROFILE_COUNT];
        settings_profiles_get(profiles);
        snprintf(l0, sizeof(l0), "%s %u", tr(lang, "PROFILE", "ПРОФИЛЬ", "预设"), s_profile + 1);
        char default_name[12];
        snprintf(default_name, sizeof(default_name), "PROFILE %u", s_profile + 1);
        if (strcmp(profiles[s_profile].name, default_name) != 0)
            snprintf(l1, sizeof(l1), "%s", profiles[s_profile].name);
        const unsigned stages = settings_profile_stage_count(&profiles[s_profile]);
        if (stages == 0) {
            snprintf(l2, sizeof(l2), "%s", tr(lang, "EMPTY", "ПУСТО", "空"));
        } else {
            snprintf(l2, sizeof(l2), "%s %u", tr(lang, "STAGES", "ЭТАПОВ", "阶段"), stages);
            char timer[16];
            format_duration(settings_profile_total_s(&profiles[s_profile]), timer);
            snprintf(l3, sizeof(l3), "%s", timer);
        }
        break;
    }
    case VIEW_PROFILE_READY: {
        cooker_profile_t profiles[COOKER_PROFILE_COUNT];
        settings_profiles_get(profiles);
        snprintf(l0, sizeof(l0), "%s %u", tr(lang, "PROFILE", "ПРОФИЛЬ", "预设"), s_profile + 1);
        snprintf(l1, sizeof(l1), "%s", profiles[s_profile].name);
        snprintf(l2, sizeof(l2), "%s %u", tr(lang, "STAGES", "ЭТАПОВ", "阶段"),
                 settings_profile_stage_count(&profiles[s_profile]));
        char timer[16];
        format_duration(settings_profile_total_s(&profiles[s_profile]), timer);
        snprintf(l3, sizeof(l3), "%s", timer);
        snprintf(l4, sizeof(l4), "%s", tr(lang, "CENTER START", "ЦЕНТР СТАРТ", "中键启动"));
        break;
    }
    case VIEW_WIFI_MENU: {
        app_settings_t current;
        settings_get(&current);
        static const char *items_en[WIFI_ITEMS] = {"POWER", "STATUS", "SETUP", "PASSWORD"};
        static const char *items_ru[WIFI_ITEMS] = {"ПИТАНИЕ", "СТАТУС", "НАСТР", "ПАРОЛЬ"};
        static const char *items_zh[WIFI_ITEMS] = {"电源", "状态", "设置", "密码"};
        const char *const *items = lang == LANG_RU ? items_ru :
                                  (lang == LANG_ZH ? items_zh : items_en);
        const char *label = s_wifi_selection == 0 ?
                            (current.wifi_enabled ? tr(lang, "ON", "ВКЛ", "开") :
                                                    tr(lang, "OFF", "ВЫКЛ", "关")) :
                            items[s_wifi_selection];
        display_prod_set_menu_overlay(s_wifi_selection + 1,
                                      label, "WI-FI");
        return;
    }
    case VIEW_WIFI_STATUS: {
        network_status_t network = {0};
        network_prod_get_status(&network);
        snprintf(l0, sizeof(l0), "WI-FI");
        if (!network.enabled) {
            snprintf(l1, sizeof(l1), "%s", tr(lang, "OFF", "ВЫКЛ", "关"));
        } else {
            snprintf(l1, sizeof(l1), "%s", network.sta_connected ?
                     tr(lang, "CONNECTED", "ПОДКЛЮЧЕНО", "已连接") :
                     tr(lang, "NO LAN", "НЕТ СЕТИ", "未连接"));
            snprintf(l2, sizeof(l2), "%s", network.sta_connected ? network.sta_ip : "-");
            snprintf(l4, sizeof(l4), "%s", network.clock_synchronized ?
                     tr(lang, "CLOCK OK", "ЧАСЫ OK", "时钟正常") :
                     tr(lang, "NO CLOCK", "НЕТ ЧАСОВ", "未同步"));
        }
        break;
    }
    case VIEW_WIFI_SETUP: {
        network_status_t network = {0};
        network_prod_get_status(&network);
        snprintf(l0, sizeof(l0), "%s", tr(lang, "WI-FI SETUP", "WI-FI НАСТР", "WI-FI设置"));
        if (!network.enabled) {
            snprintf(l2, sizeof(l2), "%s", tr(lang, "WI-FI OFF", "WI-FI ВЫКЛ", "WI-FI关闭"));
        } else {
            snprintf(l1, sizeof(l1), "%s", tr(lang, "AP READY", "ТОЧКА ГОТОВА", "热点就绪"));
            snprintf(l2, sizeof(l2), "%.10s", network.ap_ssid);
            snprintf(l3, sizeof(l3), "192.168.4.1");
            snprintf(l4, sizeof(l4), "%s", tr(lang, "OPEN WEB", "ОТКР WEB", "打开网页"));
        }
        break;
    }
    case VIEW_WIFI_PASSWORD:
        snprintf(l0, sizeof(l0), "%s", tr(lang, "AP PASSWORD", "ПАРОЛЬ AP", "热点密码"));
        snprintf(l2, sizeof(l2), "%s", network_prod_setup_password());
        snprintf(l4, sizeof(l4), "192.168.4.1");
        break;
    case VIEW_FACTORY_CONFIRM:
        snprintf(l0, sizeof(l0), "%s", tr(lang, "FACTORY", "СБРОС", "恢复出厂"));
        snprintf(l1, sizeof(l1), "%s", tr(lang, "RESET ALL", "ВСЕ НАСТР", "重置全部"));
        snprintf(l2, sizeof(l2), "%s", lang == LANG_ZH ? "WI-FI+密码" : "WI-FI+PASS");
        snprintf(l3, sizeof(l3), "%s", tr(lang, "HOLD CENTER", "ДЕРЖ ЦЕНТР", "按住中键"));
        snprintf(l4, sizeof(l4), "%s", tr(lang, "CANCEL", "ОТМЕНА", "取消"));
        break;
    }
    overlay(l0, l1, l2, l3, l4);
}

static void open_timer_action(void)
{
    cooker_snapshot_t status;
    cooking_engine_get_snapshot(&status);
    if (status.mode == COOK_MODE_PROFILE) {
        sound_play(SOUND_WARNING);
        return;
    }
    s_temperature_edit_deadline_us = 0;
    s_timer_return_view = s_view;
    s_timer_editing = true;
    if (status.timer_enabled) {
        s_view = VIEW_TIMER_DISABLE;
    } else {
        s_timer_hours = status.timer_last_s / 3600U;
        s_timer_mmss = status.timer_last_s % 3600U;
        s_view = VIEW_TIMER_MMSS;
    }
}

static void close_timer_editor(void)
{
    s_timer_editing = false;
    s_view = s_timer_return_view;
}

static void open_clock_editor(view_t return_view)
{
    const time_t now = time(NULL);
    struct tm local = {0};
    if (now > 1700000000) localtime_r(&now, &local);
    s_clock_hour = now > 1700000000 ? (unsigned)local.tm_hour : 0;
    s_clock_minute = now > 1700000000 ? (unsigned)local.tm_min : 0;
    s_clock_return_view = return_view;
    s_view = VIEW_CLOCK_HOURS;
}

static bool save_manual_clock(void)
{
    network_status_t network = {0};
    network_prod_get_status(&network);
    if (network.clock_synchronized) return true;

    time_t now = time(NULL);
    struct tm local = {0};
    if (now > 1700000000) {
        localtime_r(&now, &local);
    } else {
        /* Date is irrelevant to the UI; it only makes the offline wall clock valid. */
        local.tm_year = 124; /* 2024 */
        local.tm_mon = 0;
        local.tm_mday = 1;
    }
    local.tm_hour = (int)s_clock_hour;
    local.tm_min = (int)s_clock_minute;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    const time_t selected = mktime(&local);
    const struct timeval wall = {.tv_sec = selected, .tv_usec = 0};
    return selected > 1700000000 && settimeofday(&wall, NULL) == 0;
}

static void open_setting_value(void)
{
    app_settings_t settings;
    settings_get(&settings);
    switch (s_setting) {
    case 0: s_setting_value = settings.language; break;
    case 1: s_setting_value = settings.sound_enabled; break;
    case 2: s_setting_value = settings.show_context_value; break;
    case 3: s_setting_value = settings.show_igbt; break;
    case 4: s_setting_value = settings.timer_screen_mode == TIMER_SCREEN_ALWAYS; break;
    case 5: s_setting_value = settings.show_sleep_clock; break;
    case 6: s_setting_value = settings.sleep_minutes; break;
    case 7: s_setting_value = settings.oled_timeout_s; break;
    case 8: s_setting_value = settings.timezone_minutes; break;
    case 9: s_setting_value = settings.show_i2c_debug; break;
    }
    s_view = VIEW_SETTING_VALUE;
}

static void save_setting(void)
{
    app_settings_t settings;
    settings_get(&settings);
    switch (s_setting) {
    case 0: settings.language = s_setting_value; break;
    case 1: settings.sound_enabled = s_setting_value; break;
    case 2: settings.show_context_value = s_setting_value; break;
    case 3: settings.show_igbt = s_setting_value; break;
    case 4: settings.timer_screen_mode = s_setting_value ? TIMER_SCREEN_ALWAYS : TIMER_SCREEN_AUTO; break;
    case 5: settings.show_sleep_clock = s_setting_value; break;
    case 6: settings.sleep_minutes = s_setting_value; break;
    case 7: settings.oled_timeout_s = s_setting_value; break;
    case 8: settings.timezone_minutes = s_setting_value; break;
    case 9: settings.show_i2c_debug = s_setting_value; break;
    }
    const esp_err_t err = settings_update(&settings);
    if (err != ESP_OK) {
        sound_play(SOUND_WARNING);
        s_view = VIEW_SETTINGS;
        return;
    }
    sound_set_enabled(settings.sound_enabled);
    if (s_setting == 8) network_prod_apply_timezone();
    s_view = VIEW_SETTINGS;
    display_prod_show_confirm();
}

static bool toggle_wifi(void)
{
    app_settings_t before;
    settings_get(&before);
    app_settings_t after = before;
    after.wifi_enabled = !before.wifi_enabled;
    esp_err_t err = settings_update(&after);
    if (err == ESP_OK) err = network_prod_set_enabled(after.wifi_enabled != 0);
    if (err != ESP_OK) {
        settings_update(&before);
        sound_play(SOUND_WARNING);
        return false;
    }
    display_prod_show_confirm();
    return true;
}

static void encoder_event(const ui_input_event_t *event)
{
    const int step = encoder_step(event, s_view == VIEW_TIMER_MMSS);
    cooker_snapshot_t status;
    cooking_engine_get_snapshot(&status);
    switch (s_view) {
    case VIEW_HOME: s_selection = (unsigned)clamp((int)s_selection + (step > 0 ? 1 : -1), 0, HOME_ITEMS - 1); break;
    case VIEW_POWER: cooking_set_power(clamp((int)status.selected_gear + step, 0, 99)); break;
    case VIEW_TEMPERATURE:
        cooking_set_temperature(clamp((int)status.target_temperature_c + step,
                                      COOKER_TEMP_MIN_C, COOKER_TEMP_MAX_C));
        if (status.state == COOK_STATE_STARTING || status.state == COOK_STATE_COOKING ||
            status.state == COOK_STATE_PAUSED)
            s_temperature_edit_deadline_us = esp_timer_get_time() +
                                             TEMPERATURE_EDIT_TIMEOUT_US;
        break;
    case VIEW_SETTINGS: s_setting = (unsigned)clamp((int)s_setting + (step > 0 ? 1 : -1), 0, SETTING_ITEMS - 1); break;
    case VIEW_SETTING_VALUE:
        if (s_setting == 0)
            s_setting_value = clamp(s_setting_value + (step > 0 ? 1 : -1),
                                    LANG_EN, LANG_ZH);
        else if (s_setting <= 5 || s_setting == 9) s_setting_value = !s_setting_value;
        else if (s_setting == 6) s_setting_value = clamp(s_setting_value + (step > 0 ? 1 : -1), 1, 60);
        else if (s_setting == 7) s_setting_value = oled_timeout_step(s_setting_value, step);
        else s_setting_value = clamp(s_setting_value + (step > 0 ? 30 : -30), -720, 840);
        break;
    case VIEW_WIFI_MENU: s_wifi_selection = (unsigned)clamp((int)s_wifi_selection + (step > 0 ? 1 : -1), 0, WIFI_ITEMS - 1); break;
    case VIEW_TIMER_MMSS:
        s_timer_mmss = (uint32_t)clamp((int)s_timer_mmss + step, 0,
                                      s_timer_hours == 5 ? 0 : 3599);
        break;
    case VIEW_TIMER_HOURS:
        s_timer_hours = (uint32_t)clamp((int)s_timer_hours + (step > 0 ? 1 : -1), 0, 5);
        if (s_timer_hours == 5) s_timer_mmss = 0;
        break;
    case VIEW_TIMER_DISABLE:
        break;
    case VIEW_START_MENU: s_start_selection = (unsigned)clamp((int)s_start_selection + (step > 0 ? 1 : -1), 0, 1); break;
    case VIEW_START_IN_MINUTES:
        s_start_in_minutes = (unsigned)clamp((int)s_start_in_minutes + step, 0,
                                             s_start_in_hours == 24 ? 0 : 59);
        break;
    case VIEW_START_IN_HOURS:
        s_start_in_hours = (unsigned)clamp((int)s_start_in_hours + (step > 0 ? 1 : -1), 0, 24);
        if (s_start_in_hours == 24) s_start_in_minutes = 0;
        break;
    case VIEW_START_AT_HOURS: s_start_at_hour = (unsigned)clamp((int)s_start_at_hour + (step > 0 ? 1 : -1), 0, 23); break;
    case VIEW_START_AT_MINUTES: s_start_at_minute = (unsigned)clamp((int)s_start_at_minute + step, 0, 59); break;
    case VIEW_CLOCK_HOURS: s_clock_hour = (unsigned)clamp((int)s_clock_hour + (step > 0 ? 1 : -1), 0, 23); break;
    case VIEW_CLOCK_MINUTES: s_clock_minute = (unsigned)clamp((int)s_clock_minute + step, 0, 59); break;
    case VIEW_PROFILES: s_profile = (unsigned)clamp((int)s_profile + (step > 0 ? 1 : -1), 0, COOKER_PROFILE_COUNT - 1); break;
    default: break;
    }
}

static void central_short(void)
{
    cooker_snapshot_t status;
    cooking_engine_get_snapshot(&status);
    if (status.state == COOK_STATE_FAULT || status.state == COOK_STATE_COMPLETE) {
        cooking_acknowledge();
        s_view = VIEW_HOME;
        return;
    }
    if (s_timer_editing) {
        if (s_view == VIEW_TIMER_DISABLE) {
            cooking_timer_toggle();
            close_timer_editor();
        } else if (s_view == VIEW_TIMER_MMSS) {
            s_view = VIEW_TIMER_HOURS;
        } else {
            const uint32_t seconds = s_timer_hours * 3600U + s_timer_mmss;
            cooking_timer_set(seconds, seconds > 0);
            close_timer_editor();
        }
        return;
    }
    if (status.state == COOK_STATE_COOKING || status.state == COOK_STATE_STARTING ||
        status.state == COOK_STATE_PAUSED || status.state == COOK_STATE_NO_PAN) {
        cooking_pause_resume();
        return;
    }
    switch (s_view) {
    case VIEW_HOME:
        if (s_selection == 0) { cooking_set_mode(COOK_MODE_POWER); s_view = VIEW_POWER; }
        else if (s_selection == 1) { cooking_set_mode(COOK_MODE_TEMPERATURE); s_view = VIEW_TEMPERATURE; }
        else if (s_selection == 2) s_view = VIEW_PROFILES;
        else if (s_selection == 3) s_view = VIEW_READINGS;
        else if (s_selection == 4) { s_start_selection = 0; s_view = VIEW_START_MENU; }
        else if (s_selection == 5) { s_view = VIEW_SETTINGS; s_setting = 0; }
        else open_clock_editor(VIEW_HOME);
        break;
    case VIEW_START_MENU:
        if (s_start_selection == 0) {
            s_view = VIEW_START_IN_MINUTES;
        } else {
            if (!status.clock_valid) {
                sound_play(SOUND_WARNING);
                open_clock_editor(VIEW_START_MENU);
                break;
            }
            time_t now = time(NULL);
            struct tm local;
            localtime_r(&now, &local);
            s_start_at_hour = (local.tm_hour + 1) % 24;
            s_start_at_minute = local.tm_min;
            s_view = VIEW_START_AT_HOURS;
        }
        break;
    case VIEW_POWER:
    case VIEW_TEMPERATURE:
        cooking_start();
        break;
    case VIEW_SETTINGS:
        if (s_setting == 10) { s_wifi_selection = 0; s_view = VIEW_WIFI_MENU; }
        else if (s_setting == 11) s_view = VIEW_FACTORY_CONFIRM;
        else open_setting_value();
        break;
    case VIEW_SETTING_VALUE: save_setting(); break;
    case VIEW_TIMER_MMSS:
    case VIEW_TIMER_HOURS:
    case VIEW_TIMER_DISABLE:
        break;
    case VIEW_START_IN_MINUTES:
        s_view = VIEW_START_IN_HOURS;
        break;
    case VIEW_START_IN_HOURS: {
        const uint32_t delay_s = (s_start_in_hours * 60U + s_start_in_minutes) * 60U;
        if (delay_s > 0) {
            cooking_schedule_relative(delay_s);
            s_view = VIEW_HOME;
        } else {
            sound_play(SOUND_WARNING);
        }
        break;
    }
    case VIEW_START_AT_HOURS: s_view = VIEW_START_AT_MINUTES; break;
    case VIEW_START_AT_MINUTES: {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        local.tm_hour = s_start_at_hour;
        local.tm_min = s_start_at_minute;
        local.tm_sec = 0;
        time_t target = mktime(&local);
        if (target <= now) target += 24 * 60 * 60;
        cooking_schedule_absolute(target);
        s_view = VIEW_HOME;
        break;
    }
    case VIEW_CLOCK_HOURS:
        s_view = VIEW_CLOCK_MINUTES;
        break;
    case VIEW_CLOCK_MINUTES:
        if (save_manual_clock()) s_view = s_clock_return_view;
        else sound_play(SOUND_WARNING);
        break;
    case VIEW_PROFILES: {
        cooker_profile_t profiles[COOKER_PROFILE_COUNT];
        settings_profiles_get(profiles);
        if (settings_profile_stage_count(&profiles[s_profile]) > 0) {
            if (cooking_profile_select(s_profile) == ESP_OK)
                s_view = VIEW_PROFILE_READY;
            else
                sound_play(SOUND_WARNING);
        } else {
            sound_play(SOUND_WARNING);
        }
        break;
    }
    case VIEW_PROFILE_READY:
        cooking_start();
        break;
    case VIEW_WIFI_MENU:
        if (s_wifi_selection == 0) toggle_wifi();
        else if (s_wifi_selection == 1) s_view = VIEW_WIFI_STATUS;
        else if (s_wifi_selection == 2) s_view = VIEW_WIFI_SETUP;
        else s_view = VIEW_WIFI_PASSWORD;
        break;
    case VIEW_WIFI_STATUS:
    case VIEW_WIFI_SETUP:
    case VIEW_WIFI_PASSWORD:
        s_view = VIEW_WIFI_MENU;
        break;
    case VIEW_FACTORY_CONFIRM:
        sound_play(SOUND_WARNING);
        break;
    case VIEW_READINGS: s_view = VIEW_HOME; break;
    }
}

static bool central_long(void)
{
    cooker_snapshot_t status;
    cooking_engine_get_snapshot(&status);
    s_temperature_edit_deadline_us = 0;
    if (s_timer_editing) {
        close_timer_editor();
    } else if (status.state == COOK_STATE_DELAYED) {
        cooking_schedule_cancel();
        s_view = VIEW_HOME;
    } else if (status.state == COOK_STATE_FAULT) {
        cooking_acknowledge();
        s_view = VIEW_HOME;
    } else if (status.state == COOK_STATE_COOKING || status.state == COOK_STATE_STARTING ||
               status.state == COOK_STATE_PAUSED || status.state == COOK_STATE_NO_PAN) {
        cooking_stop("CENTER HOLD");
        s_view = VIEW_HOME;
    } else if (s_view == VIEW_FACTORY_CONFIRM) {
        app_settings_t before_reset;
        settings_get(&before_reset);
        const esp_err_t reset = settings_factory_reset();
        if (reset != ESP_OK) {
            sound_play(SOUND_WARNING);
            return true;
        }
        const char *lines[UI_OLED_TEXT_LINES] = {
            tr(before_reset.language, "FACTORY RESET", "СБРОС НАСТР", "恢复出厂"),
            tr(before_reset.language, "DONE", "ГОТОВО", "完成"),
            tr(before_reset.language, "WI-FI OFF", "WI-FI ВЫКЛ", "WI-FI关闭"),
            "",
            tr(before_reset.language, "RESTART", "ПЕРЕЗАПУСК", "重启")
        };
        display_prod_set_overlay(lines);
        sound_play(SOUND_SLEEP);
        vTaskDelay(pdMS_TO_TICKS(700));
        esp_restart();
    } else if (s_view == VIEW_HOME) {
        remember_primary_wake_selection();
        cooking_sleep();
        display_prod_clear_overlay();
        sound_play(SOUND_SLEEP);
        return false;
    } else if (s_view == VIEW_CLOCK_HOURS || s_view == VIEW_CLOCK_MINUTES) {
        s_view = s_clock_return_view;
    } else {
        s_view = VIEW_HOME;
    }
    return true;
}

static void cancel_action(void)
{
    cooker_snapshot_t status;
    cooking_engine_get_snapshot(&status);
    s_temperature_edit_deadline_us = 0;
    s_timer_editing = false;
    if (status.state == COOK_STATE_FAULT || status.state == COOK_STATE_COMPLETE)
        cooking_acknowledge();
    else if (status.state == COOK_STATE_DELAYED) cooking_schedule_cancel();
    else if (status.state == COOK_STATE_COOKING || status.state == COOK_STATE_STARTING ||
             status.state == COOK_STATE_PAUSED || status.state == COOK_STATE_NO_PAN)
        cooking_stop("CANCEL");
    s_view = VIEW_HOME;
    display_prod_show_cancel();
}

static void input_event(const ui_input_event_t *event)
{
    const int64_t now = esp_timer_get_time();
    s_last_input_us = now;
    cooker_snapshot_t input_status;
    cooking_engine_get_snapshot(&input_status);
    if (input_status.state == COOK_STATE_SLEEP && event->type == UI_INPUT_MAIN_PRESSED) {
        cooking_wake();
        display_prod_activity();
        restore_primary_wake_selection();
        s_swallow_main = true;
        return;
    }
    if (input_status.state == COOK_STATE_SLEEP && event->type == UI_INPUT_ENCODER) {
        cooking_wake();
        display_prod_activity();
        restore_primary_wake_selection();
        s_encoder_guard_until_us = now + COOKER_ENCODER_WAKE_GUARD_MS * 1000LL;
        return;
    }
    if (event->type == UI_INPUT_TOUCH_A_PRESSED || event->type == UI_INPUT_TOUCH_BOTH_PRESSED) {
        if (input_status.state == COOK_STATE_SLEEP) {
            cooking_wake();
            display_prod_activity();
            restore_primary_wake_selection();
            sound_play(SOUND_UI_CLICK);
            return;
        }
        cancel_action();
        display_prod_activity();
        sound_play(SOUND_UI_CLICK);
        return;
    }
    if (event->type == UI_INPUT_MAIN_PRESSED && !display_prod_is_awake()) {
        display_prod_activity();
        cooking_wake();
        s_swallow_main = true;
        return;
    }
    if (event->type == UI_INPUT_ENCODER && !display_prod_is_awake()) {
        display_prod_activity();
        cooking_wake();
        s_encoder_guard_until_us = now + COOKER_ENCODER_WAKE_GUARD_MS * 1000LL;
        return;
    }
    if (event->type == UI_INPUT_ENCODER && now < s_encoder_guard_until_us) return;
    display_prod_activity();
    if (event->type == UI_INPUT_ENCODER) encoder_event(event);
    else if (event->type == UI_INPUT_MAIN_LONG) {
        if (s_swallow_main) return;
        if (central_long()) sound_play(SOUND_UI_CLICK);
        s_swallow_main = true;
    }
    else if (event->type == UI_INPUT_MAIN_RELEASED) {
        if (s_swallow_main) {
            s_swallow_main = false;
            return;
        }
        central_short();
        sound_play(SOUND_UI_CLICK);
    } else if (event->type == UI_INPUT_TOUCH_B_PRESSED) {
        if (input_status.state == COOK_STATE_SLEEP) {
            cooking_wake();
            display_prod_activity();
            restore_primary_wake_selection();
            s_swallow_timer = true;
            return;
        }
    } else if (event->type == UI_INPUT_TOUCH_B_RELEASED) {
        if (s_swallow_timer) { s_swallow_timer = false; return; }
        if (s_timer_editing) close_timer_editor();
        else open_timer_action();
        sound_play(SOUND_UI_CLICK);
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    s_last_input_us = esp_timer_get_time();
    s_view = VIEW_HOME;
    for (;;) {
        ui_input_event_t event;
        if (ui_inputs_get_event(&event, pdMS_TO_TICKS(100))) input_event(&event);
        cooker_snapshot_t status;
        app_settings_t settings;
        cooking_engine_get_snapshot(&status);
        settings_get(&settings);
        const int64_t now = esp_timer_get_time();
        const bool urgent_screen = status.state == COOK_STATE_FAULT ||
                                   status.state == COOK_STATE_COMPLETE ||
                                   status.state == COOK_STATE_NO_PAN ||
                                   status.hold_saturated;
        if (urgent_screen) s_temperature_edit_deadline_us = 0;
        if (urgent_screen && s_timer_editing) {
            close_timer_editor();
        }
        if (s_timer_editing && now - s_last_input_us >= EDITOR_TIMEOUT_US) {
            close_timer_editor();
        }
        if (s_temperature_edit_deadline_us != 0 &&
            now >= s_temperature_edit_deadline_us) {
            s_temperature_edit_deadline_us = 0;
        }
        if ((status.state == COOK_STATE_IDLE || status.state == COOK_STATE_READY) &&
            now - s_last_input_us >= (int64_t)settings.sleep_minutes * 60 * 1000000LL) {
            remember_primary_wake_selection();
            cooking_sleep();
            display_prod_clear_overlay();
            sound_play(SOUND_SLEEP);
            s_last_input_us = now;
        } else if (status.state != COOK_STATE_SLEEP) {
            const bool live = status.state == COOK_STATE_DELAYED ||
                              status.state == COOK_STATE_STARTING ||
                              status.state == COOK_STATE_COOKING ||
                              status.state == COOK_STATE_PAUSED ||
                              status.state == COOK_STATE_NO_PAN ||
                              status.state == COOK_STATE_COMPLETE ||
                              status.state == COOK_STATE_FAULT;
            const bool temperature_editing = s_temperature_edit_deadline_us != 0 &&
                                             s_view == VIEW_TEMPERATURE;
            if (live && !s_timer_editing && !temperature_editing)
                display_prod_clear_overlay();
            else render();
        }
    }
}

esp_err_t ui_controller_init(void)
{
    return xTaskCreate(ui_task, "ui_controller", 6144, NULL, 5, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

bool ui_controller_timer_editing(void) { return s_timer_editing; }
bool ui_controller_setpoint_editing(void)
{
    return s_view == VIEW_POWER || s_view == VIEW_TEMPERATURE;
}
