#include "display_prod.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "cooking_engine.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "oled_assets.h"
#include "settings.h"

#define DISPLAY_LINE_BYTES 32

typedef enum {
    OVERLAY_TEXT = 0,
    OVERLAY_MENU,
    OVERLAY_FOCUS,
    OVERLAY_TEMPERATURE_EDIT,
    OVERLAY_TIME_EDITOR,
    OVERLAY_INFO,
    OVERLAY_VERSION,
} overlay_kind_t;

typedef enum {
    TRANSIENT_NONE = 0,
    TRANSIENT_TURN_ON,
    TRANSIENT_WAKEUP,
    TRANSIENT_COOKING,
    TRANSIENT_CONFIRM,
    TRANSIENT_CANCEL,
} transient_image_t;

static SemaphoreHandle_t s_lock;
static bool s_overlay;
static overlay_kind_t s_overlay_kind;
static char s_overlay_lines[UI_OLED_TEXT_LINES][DISPLAY_LINE_BYTES];
static char s_overlay_a[DISPLAY_LINE_BYTES];
static char s_overlay_b[DISPLAY_LINE_BYTES];
static char s_overlay_c[DISPLAY_LINE_BYTES];
static char s_overlay_d[DISPLAY_LINE_BYTES];
static unsigned s_overlay_value;
static unsigned s_overlay_ntc;
static unsigned s_overlay_igbt;
static bool s_overlay_valid;
static bool s_overlay_degree;
static int64_t s_last_activity_us;
static int64_t s_sleep_clock_anchor_minute = -1;
static int64_t s_sleep_entered_us;
static transient_image_t s_transient_image;
static int64_t s_transient_deadline_us;
static cook_state_t s_previous_state;
static bool s_previous_state_valid;
static bool s_awake;
static unsigned s_i2c_debug_peak;
static int64_t s_i2c_debug_hold_until_us;

_Static_assert(OLED_ASSET_FRAME_BYTES == UI_OLED_BITMAP_BYTES,
               "OLED asset and driver frame sizes must match");

static bool active_picture_state(cook_state_t state)
{
    return state == COOK_STATE_STARTING || state == COOK_STATE_COOKING;
}

static unsigned i2c_debug_display_value(unsigned current, int64_t now_us)
{
    if (current > COOKER_I2C_DEBUG_MAX) current = COOKER_I2C_DEBUG_MAX;
    if (current > 0) {
        if (current > s_i2c_debug_peak) s_i2c_debug_peak = current;
        s_i2c_debug_hold_until_us = now_us +
            (int64_t)COOKER_I2C_DEBUG_HOLD_MS * 1000LL;
    } else if (s_i2c_debug_hold_until_us != 0 &&
               now_us >= s_i2c_debug_hold_until_us) {
        s_i2c_debug_peak = 0;
        s_i2c_debug_hold_until_us = 0;
    }
    return current > s_i2c_debug_peak ? current : s_i2c_debug_peak;
}

static void set_transient_locked(transient_image_t image, uint32_t duration_ms,
                                 int64_t now_us)
{
    s_transient_image = image;
    s_transient_deadline_us = image == TRANSIENT_NONE ? 0 :
                             now_us + (int64_t)duration_ms * 1000LL;
}

static const uint8_t *transient_bitmap(transient_image_t image)
{
    switch (image) {
    case TRANSIENT_TURN_ON: return oled_image_turn_on;
    case TRANSIENT_WAKEUP: return oled_image_wakeup;
    case TRANSIENT_COOKING: return oled_image_cooking;
    case TRANSIENT_CONFIRM: return oled_image_confirm;
    case TRANSIENT_CANCEL: return oled_image_cancel;
    default: return NULL;
    }
}

static void format_fault_code(cooker_fault_t fault, char output[4])
{
    const char *code = "E??";
    switch (fault) {
    case FAULT_E02_NO_PAN_TIMEOUT: code = "E02"; break;
    case FAULT_E03_HIGH_VOLTAGE: code = "E03"; break;
    case FAULT_E04_LOW_VOLTAGE: code = "E04"; break;
    case FAULT_E05_BOTTOM_OVERHEAT: code = "E05"; break;
    case FAULT_E07_IGBT_OVERHEAT: code = "E07"; break;
    case FAULT_E08_SENSOR: code = "E08"; break;
    case FAULT_E09_COMMUNICATION: code = "E09"; break;
    case FAULT_E10_WIRE_OR_CHANNEL: code = "E10"; break;
    case FAULT_E12_POWER_STATUS: code = "E12"; break;
    case FAULT_POWER_STATUS: code = "EPB"; break;
    case FAULT_START_TIMEOUT: code = "EST"; break;
    case FAULT_HARD_RUN_LIMIT: code = "ETM"; break;
    default: break;
    }
    memcpy(output, code, 4);
}

static const char *tr(app_language_t language, const char *english,
                      const char *russian, const char *chinese)
{
    if (language == LANG_RU) return russian;
    if (language == LANG_ZH) return chinese;
    return english;
}

static const char *mode_name(cook_mode_t mode, app_language_t language)
{
    switch (mode) {
    case COOK_MODE_POWER: return tr(language, "POWER", "МОЩНОСТЬ", "功率");
    case COOK_MODE_TEMPERATURE: return tr(language, "TEMP", "ТЕМП", "温度");
    case COOK_MODE_PROFILE: return tr(language, "PROFILE", "ПРОФИЛЬ", "预设");
    default: return "?";
    }
}

static const char *state_name(cook_state_t state, app_language_t language)
{
    if (language == LANG_EN) return cooking_state_name(state);
    if (language == LANG_ZH) {
        switch (state) {
        case COOK_STATE_SLEEP: return "休眠";
        case COOK_STATE_IDLE: return "就绪";
        case COOK_STATE_READY: return "设置";
        case COOK_STATE_DELAYED: return "等待";
        case COOK_STATE_STARTING: return "启动";
        case COOK_STATE_COOKING: return "加热";
        case COOK_STATE_PAUSED: return "暂停";
        case COOK_STATE_NO_PAN: return "无锅";
        case COOK_STATE_COMPLETE: return "完成";
        case COOK_STATE_FAULT: return "故障";
        default: return "?";
        }
    }
    switch (state) {
    case COOK_STATE_SLEEP: return "СОН";
    case COOK_STATE_IDLE: return "ГОТОВО";
    case COOK_STATE_READY: return "НАСТРОЙКА";
    case COOK_STATE_DELAYED: return "ОЖИДАНИЕ";
    case COOK_STATE_STARTING: return "ЗАПУСК";
    case COOK_STATE_COOKING: return "НАГРЕВ";
    case COOK_STATE_PAUSED: return "ПАУЗА";
    case COOK_STATE_NO_PAN: return "НЕТ ПОСУД";
    case COOK_STATE_COMPLETE: return "ГОТОВО";
    case COOK_STATE_FAULT: return "ОШИБКА";
    default: return "?";
    }
}

static void format_time(uint32_t seconds, char output[16])
{
    const unsigned hours = seconds / 3600U;
    const unsigned minutes = (seconds / 60U) % 60U;
    const unsigned secs = seconds % 60U;
    if (hours) snprintf(output, 16, "%lu:%02u:%02u", (unsigned long)hours, minutes, secs);
    else snprintf(output, 16, "%02u:%02u", minutes, secs);
}

static bool format_timer_compact(uint32_t seconds, char output[16])
{
    const unsigned hours = seconds / 3600U;
    const unsigned minutes = (seconds / 60U) % 60U;
    const unsigned secs = seconds % 60U;
    if (hours > 0) {
        snprintf(output, 16, "%u:%02u", hours, minutes);
        return false; /* Single minute prime. */
    }
    snprintf(output, 16, "%02u:%02u", minutes, secs);
    return true; /* Double second prime. */
}

static void normal_screen(const cooker_snapshot_t *s, const app_settings_t *settings,
                          char lines[UI_OLED_TEXT_LINES][DISPLAY_LINE_BYTES])
{
    const app_language_t lang = settings->language;
    snprintf(lines[0], DISPLAY_LINE_BYTES, "%s", mode_name(s->mode, lang));
    if (s->delayed_start) {
        char time_text[16];
        format_time(s->delayed_remaining_s, time_text);
        snprintf(lines[1], DISPLAY_LINE_BYTES, "%s", tr(lang, "DELAY", "ОТЛОЖ", "延时"));
        if (s->mode == COOK_MODE_TEMPERATURE)
            snprintf(lines[2], DISPLAY_LINE_BYTES,
                     tr(lang, "SET %u°C", "ЦЕЛЬ %u°C", "目标 %u°C"),
                     s->target_temperature_c);
        else
            snprintf(lines[2], DISPLAY_LINE_BYTES, "%u", s->selected_gear);
        snprintf(lines[3], DISPLAY_LINE_BYTES, "%s", time_text);
        return;
    }
    snprintf(lines[1], DISPLAY_LINE_BYTES, "%s", state_name(s->state, lang));
    if (s->mode == COOK_MODE_TEMPERATURE) {
        snprintf(lines[2], DISPLAY_LINE_BYTES, tr(lang, "SET %3uC", "ЦЕЛЬ %3uC", "目标 %3uC"),
                 s->target_temperature_c);
        snprintf(lines[3], DISPLAY_LINE_BYTES, tr(lang, "NOW %3uC", "ТЕК %3uC", "当前 %3uC"),
                 s->bottom_c);
    } else {
        snprintf(lines[2], DISPLAY_LINE_BYTES, tr(lang, "GEAR %02u", "УРОВ %02u", "档位 %02u"),
                 s->selected_gear);
        snprintf(lines[3], DISPLAY_LINE_BYTES, tr(lang, "OUT  %02u", "ВЫХ  %02u", "输出 %02u"),
                 s->applied_gear);
    }
    if (s->hold_saturated) {
        snprintf(lines[4], DISPLAY_LINE_BYTES, "%s",
                 tr(lang, "HOLD LIMIT", "ПРЕДЕЛ 35", "已达上限35"));
    } else if (s->timer_enabled) {
        char time_text[16];
        format_time(s->timer_remaining_s, time_text);
        snprintf(lines[4], DISPLAY_LINE_BYTES, "T %s", time_text);
    }
}

static void display_task(void *arg)
{
    (void)arg;
    for (;;) {
        cooker_snapshot_t cooker;
        app_settings_t settings;
        cooking_engine_get_snapshot(&cooker);
        settings_get(&settings);
        const int64_t now = esp_timer_get_time();
        bool show;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool overlay = s_overlay;
        const overlay_kind_t overlay_kind = s_overlay_kind;
        const unsigned overlay_value = s_overlay_value;
        const unsigned overlay_ntc = s_overlay_ntc;
        const unsigned overlay_igbt = s_overlay_igbt;
        const bool overlay_valid = s_overlay_valid;
        const bool overlay_degree = s_overlay_degree;
        char lines[UI_OLED_TEXT_LINES][DISPLAY_LINE_BYTES] = {{0}};
        char overlay_a[DISPLAY_LINE_BYTES] = {0};
        char overlay_b[DISPLAY_LINE_BYTES] = {0};
        char overlay_c[DISPLAY_LINE_BYTES] = {0};
        char overlay_d[DISPLAY_LINE_BYTES] = {0};
        if (overlay) memcpy(lines, s_overlay_lines, sizeof(lines));
        if (overlay) {
            strlcpy(overlay_a, s_overlay_a, sizeof(overlay_a));
            strlcpy(overlay_b, s_overlay_b, sizeof(overlay_b));
            strlcpy(overlay_c, s_overlay_c, sizeof(overlay_c));
            strlcpy(overlay_d, s_overlay_d, sizeof(overlay_d));
        }
        if (!s_previous_state_valid) {
            s_previous_state = cooker.state;
            s_previous_state_valid = true;
        } else if (s_previous_state != cooker.state) {
            const cook_state_t previous = s_previous_state;
            s_previous_state = cooker.state;
            if (cooker.state == COOK_STATE_SLEEP) {
                s_sleep_entered_us = now;
                set_transient_locked(TRANSIENT_NONE, 0, now);
            } else {
                if (previous == COOK_STATE_SLEEP)
                    set_transient_locked(TRANSIENT_WAKEUP, COOKER_IMAGE_WAKEUP_MS, now);
                if (active_picture_state(cooker.state) && !active_picture_state(previous))
                    set_transient_locked(TRANSIENT_COOKING, COOKER_IMAGE_COOKING_MS, now);
                if (cooker.state == COOK_STATE_FAULT ||
                    cooker.state == COOK_STATE_COMPLETE ||
                    cooker.state == COOK_STATE_NO_PAN)
                    set_transient_locked(TRANSIENT_NONE, 0, now);
            }
        }
        if (s_transient_image != TRANSIENT_NONE && now >= s_transient_deadline_us)
            set_transient_locked(TRANSIENT_NONE, 0, now);
        const int64_t timeout_us = (int64_t)settings.oled_timeout_s * 1000000LL;
        const time_t wall = time(NULL);
        const bool sleep_clock = cooker.state == COOK_STATE_SLEEP &&
                                 settings.show_sleep_clock && wall > 1700000000;
        if (!sleep_clock) s_sleep_clock_anchor_minute = -1;
        else if (s_sleep_clock_anchor_minute < 0)
            s_sleep_clock_anchor_minute = (int64_t)wall / 60;
        const int64_t idle_sleep_us = (int64_t)settings.sleep_minutes * 60 * 1000000LL;
        const bool sleep_warning =
            (cooker.state == COOK_STATE_IDLE || cooker.state == COOK_STATE_READY) &&
            idle_sleep_us >= (int64_t)COOKER_IMAGE_SLEEP_WARNING_MS * 1000LL &&
            now - s_last_activity_us >=
                idle_sleep_us - (int64_t)COOKER_IMAGE_SLEEP_WARNING_MS * 1000LL;
        const bool sleep_picture = cooker.state == COOK_STATE_SLEEP &&
            s_sleep_entered_us != 0 &&
            now - s_sleep_entered_us < (int64_t)COOKER_IMAGE_SLEEP_MS * 1000LL;
        const uint8_t *picture = NULL;
        bool picture_has_fault_code = false;
        char fault_code[4] = {0};
        if (cooker.state == COOK_STATE_FAULT) {
            picture = oled_image_error;
            picture_has_fault_code = true;
            format_fault_code(cooker.fault, fault_code);
        } else if (cooker.state == COOK_STATE_NO_PAN) {
            picture = oled_image_no_pan;
        } else if (cooker.state == COOK_STATE_COMPLETE) {
            picture = oled_image_ready;
        } else if (s_transient_image != TRANSIENT_NONE) {
            picture = transient_bitmap(s_transient_image);
        } else if (sleep_picture) {
            picture = oled_image_sleep;
        } else if (sleep_warning) {
            picture = oled_image_sleep_warning;
        }
        show = picture != NULL || sleep_clock || cooker.state == COOK_STATE_FAULT ||
               cooker.state == COOK_STATE_COMPLETE ||
               cooker.state == COOK_STATE_NO_PAN || cooker.hold_saturated ||
               (settings.timer_screen_mode == TIMER_SCREEN_ALWAYS && cooker.timer_enabled &&
                !cooker.hold_saturated &&
                cooker.state == COOK_STATE_COOKING) ||
               (cooker.state != COOK_STATE_SLEEP && now - s_last_activity_us < timeout_us);
        if (show != s_awake) {
            ui_oled_power(show);
            s_awake = show;
        }
        xSemaphoreGive(s_lock);

        if (show) {
            const bool urgent = cooker.state == COOK_STATE_FAULT ||
                                cooker.state == COOK_STATE_COMPLETE ||
                                cooker.state == COOK_STATE_NO_PAN ||
                                cooker.hold_saturated;
            const bool effective_overlay = overlay && !urgent;
            const bool timer_only_due = settings.timer_screen_mode == TIMER_SCREEN_ALWAYS &&
                                        cooker.timer_enabled && !urgent &&
                                        cooker.state == COOK_STATE_COOKING &&
                                        now - s_last_activity_us >= timeout_us;
            const bool active_focus = !effective_overlay && !timer_only_due && !urgent &&
                                      (cooker.state == COOK_STATE_STARTING ||
                                       cooker.state == COOK_STATE_COOKING ||
                                       cooker.state == COOK_STATE_PAUSED);
            if (!effective_overlay && !active_focus && picture == NULL) {
                if (!timer_only_due) normal_screen(&cooker, &settings, lines);
            }
            if (picture != NULL) {
                if (picture_has_fault_code)
                    ui_oled_show_bitmap_text(picture, fault_code, 30, 32, 2);
                else
                    ui_oled_show_bitmap(picture);
            } else if (sleep_clock) {
                struct tm local = {0};
                char clock_text[8];
                localtime_r(&wall, &local);
                snprintf(clock_text, sizeof(clock_text), "%02d:%02d",
                         local.tm_hour, local.tm_min);
                const int64_t minute = (int64_t)wall / 60;
                const uint32_t elapsed = minute >= s_sleep_clock_anchor_minute ?
                                         (uint32_t)(minute - s_sleep_clock_anchor_minute) : 0;
                ui_oled_show_sleep_clock(clock_text, elapsed % 42U,
                                         (elapsed / 42U) % 3U);
            } else if (effective_overlay && overlay_kind == OVERLAY_MENU) {
                ui_oled_show_menu_item(overlay_value, overlay_a, overlay_b);
            } else if (effective_overlay && overlay_kind == OVERLAY_FOCUS) {
                ui_oled_show_focus(overlay_a, overlay_b, overlay_c,
                                   overlay_degree, overlay_d);
            } else if (effective_overlay && overlay_kind == OVERLAY_TEMPERATURE_EDIT) {
                ui_oled_show_temperature_editor(overlay_value, overlay_ntc, overlay_valid);
            } else if (effective_overlay && overlay_kind == OVERLAY_TIME_EDITOR) {
                ui_oled_show_time_editor(overlay_a, overlay_b, overlay_c);
            } else if (effective_overlay && overlay_kind == OVERLAY_INFO) {
                ui_oled_show_info(overlay_value, overlay_ntc, overlay_igbt, overlay_valid);
            } else if (effective_overlay && overlay_kind == OVERLAY_VERSION) {
                ui_oled_show_version(overlay_a, overlay_b);
            } else if (timer_only_due) {
                char timer[16];
                const bool timer_seconds = format_timer_compact(cooker.timer_remaining_s, timer);
                ui_oled_show_timer(timer, timer_seconds, (cooker.run_elapsed_s / 60U) % 3U);
            } else if (active_focus) {
                char top_left[DISPLAY_LINE_BYTES] = {0};
                char top_right[DISPLAY_LINE_BYTES] = {0};
                char value[12] = {0};
                char timer[16] = {0};
                char bottom[16] = {0};
                const bool paused = cooker.state == COOK_STATE_PAUSED;
                const bool timer_active = cooker.timer_enabled && !paused;
                bool timer_seconds = false;
                bool degree = false;
                const cook_mode_t active_mode = cooker.mode == COOK_MODE_PROFILE ?
                                                (cook_mode_t)cooker.profile_stage_mode :
                                                cooker.mode;
                const bool profile = cooker.mode == COOK_MODE_PROFILE;
                const bool show_igbt_phase = settings.show_igbt && !timer_active && !paused &&
                                             ((now / 1000000LL) % 7LL) >= 5LL;
                if (active_mode == COOK_MODE_TEMPERATURE) {
                    snprintf(top_left, sizeof(top_left), "S%u°", cooker.target_temperature_c);
                    if (cooker.readings_valid) snprintf(value, sizeof(value), "%u", cooker.bottom_c);
                    else strlcpy(value, "--", sizeof(value));
                    degree = cooker.readings_valid;
                    if (profile) {
                        snprintf(top_right, sizeof(top_right), "%u/%u",
                                 cooker.profile_stage_index, cooker.profile_stage_count);
                    } else if (show_igbt_phase) {
                        if (cooker.readings_valid) snprintf(top_right, sizeof(top_right), "I%u°", cooker.igbt_c);
                        else strlcpy(top_right, "I--", sizeof(top_right));
                    } else if (!paused && settings.show_context_value) {
                        snprintf(top_right, sizeof(top_right), "P%u", cooker.applied_gear);
                    }
                } else {
                    snprintf(value, sizeof(value), "%u", cooker.selected_gear);
                    if (profile)
                        snprintf(top_left, sizeof(top_left), "%u/%u",
                                 cooker.profile_stage_index, cooker.profile_stage_count);
                    if (show_igbt_phase) {
                        if (cooker.readings_valid) snprintf(top_right, sizeof(top_right), "I%u°", cooker.igbt_c);
                        else strlcpy(top_right, "I--", sizeof(top_right));
                    } else if (!paused && settings.show_context_value) {
                        if (cooker.readings_valid) snprintf(top_right, sizeof(top_right), "N%u°", cooker.bottom_c);
                        else strlcpy(top_right, "N--", sizeof(top_right));
                    }
                }
                if (paused) strlcpy(bottom,
                                    tr(settings.language, "PAUSE", "ПАУЗА", "暂停"),
                                    sizeof(bottom));
                if (timer_active)
                    timer_seconds = format_timer_compact(cooker.timer_remaining_s, timer);
                ui_oled_show_cooking(top_left, top_right, value, degree, timer,
                                     timer_seconds, bottom);
            } else {
                const char *pointers[UI_OLED_TEXT_LINES];
                for (unsigned i = 0; i < UI_OLED_TEXT_LINES; ++i) pointers[i] = lines[i];
                ui_oled_show_text(pointers);
            }
            if (settings.show_i2c_debug) {
                ui_oled_overlay_debug_counter(
                    i2c_debug_display_value(cooker.i2c_bad_cycles, now));
            } else {
                s_i2c_debug_peak = 0;
                s_i2c_debug_hold_until_us = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t display_prod_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    /* Keep OLED blank until ui_controller publishes the first real screen. */
    memset(s_overlay_lines, 0, sizeof(s_overlay_lines));
    s_overlay_kind = OVERLAY_TEXT;
    s_overlay = true;
    s_last_activity_us = esp_timer_get_time();
    s_sleep_entered_us = 0;
    s_transient_image = TRANSIENT_TURN_ON;
    s_transient_deadline_us = s_last_activity_us +
                              (int64_t)COOKER_IMAGE_TURN_ON_MS * 1000LL;
    s_previous_state_valid = false;
    s_awake = true;
    return xTaskCreate(display_task, "display", 4096, NULL, 3, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

void display_prod_activity(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_last_activity_us = esp_timer_get_time();
    if (!s_awake) {
        ui_oled_power(true);
        s_awake = true;
    }
    xSemaphoreGive(s_lock);
}

bool display_prod_is_awake(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool awake = s_awake;
    xSemaphoreGive(s_lock);
    return awake;
}

void display_prod_show_confirm(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_transient_locked(TRANSIENT_CONFIRM, COOKER_IMAGE_CONFIRM_MS, esp_timer_get_time());
    xSemaphoreGive(s_lock);
}

void display_prod_show_cancel(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_transient_locked(TRANSIENT_CANCEL, COOKER_IMAGE_CANCEL_MS, esp_timer_get_time());
    xSemaphoreGive(s_lock);
}

void display_prod_set_overlay(const char *lines[UI_OLED_TEXT_LINES])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_overlay_lines, 0, sizeof(s_overlay_lines));
    for (unsigned i = 0; i < UI_OLED_TEXT_LINES; ++i)
        if (lines != NULL && lines[i] != NULL)
            strlcpy(s_overlay_lines[i], lines[i], sizeof(s_overlay_lines[i]));
    s_overlay_kind = OVERLAY_TEXT;
    s_overlay_a[0] = '\0';
    s_overlay_b[0] = '\0';
    s_overlay_c[0] = '\0';
    s_overlay_d[0] = '\0';
    s_overlay_degree = false;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_set_menu_overlay(unsigned index, const char *label, const char *subtitle)
{
    if (label == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_overlay_kind = OVERLAY_MENU;
    s_overlay_value = index;
    strlcpy(s_overlay_a, label, sizeof(s_overlay_a));
    strlcpy(s_overlay_b, subtitle == NULL ? "" : subtitle, sizeof(s_overlay_b));
    s_overlay_c[0] = '\0';
    s_overlay_d[0] = '\0';
    s_overlay_degree = false;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_set_number_overlay(unsigned value)
{
    char text[8];
    snprintf(text, sizeof(text), "%u", value);
    display_prod_set_focus_overlay("", "", text, false, "");
}

void display_prod_set_focus_overlay(const char *top_left, const char *top_right,
                                    const char *value, bool degree, const char *bottom)
{
    if (value == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_overlay_kind = OVERLAY_FOCUS;
    strlcpy(s_overlay_a, top_left == NULL ? "" : top_left, sizeof(s_overlay_a));
    strlcpy(s_overlay_b, top_right == NULL ? "" : top_right, sizeof(s_overlay_b));
    strlcpy(s_overlay_c, value, sizeof(s_overlay_c));
    strlcpy(s_overlay_d, bottom == NULL ? "" : bottom, sizeof(s_overlay_d));
    s_overlay_degree = degree;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_set_temperature_edit_overlay(unsigned setpoint_c, unsigned current_c,
                                               bool current_valid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_overlay_kind = OVERLAY_TEMPERATURE_EDIT;
    s_overlay_value = setpoint_c;
    s_overlay_ntc = current_c;
    s_overlay_valid = current_valid;
    s_overlay_a[0] = '\0';
    s_overlay_b[0] = '\0';
    s_overlay_c[0] = '\0';
    s_overlay_d[0] = '\0';
    s_overlay_degree = false;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_set_time_editor_overlay(const char *title, const char *value,
                                          const char *footer)
{
    if (value == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_overlay_kind = OVERLAY_TIME_EDITOR;
    strlcpy(s_overlay_a, title == NULL ? "" : title, sizeof(s_overlay_a));
    strlcpy(s_overlay_b, value, sizeof(s_overlay_b));
    strlcpy(s_overlay_c, footer == NULL ? "" : footer, sizeof(s_overlay_c));
    s_overlay_d[0] = '\0';
    s_overlay_degree = false;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_set_info_overlay(unsigned voltage_v, unsigned ntc_c, unsigned igbt_c, bool valid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_overlay_kind = OVERLAY_INFO;
    s_overlay_value = voltage_v;
    s_overlay_ntc = ntc_c;
    s_overlay_igbt = igbt_c;
    s_overlay_valid = valid;
    s_overlay_a[0] = '\0';
    s_overlay_b[0] = '\0';
    s_overlay_c[0] = '\0';
    s_overlay_d[0] = '\0';
    s_overlay_degree = false;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_set_version_overlay(const char *title, const char *version)
{
    if (title == NULL || version == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_overlay_kind = OVERLAY_VERSION;
    strlcpy(s_overlay_a, title, sizeof(s_overlay_a));
    strlcpy(s_overlay_b, version, sizeof(s_overlay_b));
    s_overlay_c[0] = '\0';
    s_overlay_d[0] = '\0';
    s_overlay_degree = false;
    s_overlay = true;
    xSemaphoreGive(s_lock);
}

void display_prod_clear_overlay(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_overlay) {
        s_overlay = false;
        s_last_activity_us = esp_timer_get_time();
    }
    xSemaphoreGive(s_lock);
}
