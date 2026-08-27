#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define UI_OLED_TEXT_LINES 5
#define UI_OLED_TEXT_COLUMNS 10
#define UI_OLED_BITMAP_BYTES (64U * 6U)

esp_err_t ui_outputs_init(void);
void ui_outputs_all_off(const char *reason);

esp_err_t ui_led_raw_pulse(unsigned index, uint8_t value, unsigned duration_ms);
esp_err_t ui_led_power_level(unsigned level, unsigned duration_ms);
esp_err_t ui_direct_output_pulse(unsigned gpio, unsigned level, unsigned duration_ms);
esp_err_t ui_led_raw_set(unsigned index, uint8_t value);
esp_err_t ui_led_power_set(unsigned level);
esp_err_t ui_led_panel_set(unsigned power_level, bool orange, bool blue);
esp_err_t ui_direct_output_set(unsigned gpio, unsigned level);
esp_err_t ui_oled_show_pattern(unsigned pattern);
esp_err_t ui_oled_show_bitmap(const uint8_t bitmap[UI_OLED_BITMAP_BYTES]);
esp_err_t ui_oled_show_bitmap_text(const uint8_t bitmap[UI_OLED_BITMAP_BYTES],
                                   const char *text, int x, int y);
esp_err_t ui_oled_overlay_debug_counter(unsigned value);
esp_err_t ui_oled_show_text(const char *lines[UI_OLED_TEXT_LINES]);
esp_err_t ui_oled_show_menu_item(unsigned index, const char *label, const char *subtitle);
esp_err_t ui_oled_show_large_number(unsigned value);
esp_err_t ui_oled_show_focus(const char *top_left, const char *top_right,
                             const char *value, bool degree, const char *bottom);
esp_err_t ui_oled_show_temperature_editor(unsigned setpoint_c, unsigned current_c,
                                          bool current_valid);
esp_err_t ui_oled_show_cooking(const char *top_left, const char *top_right,
                               const char *value, bool degree, const char *timer,
                               bool timer_seconds, const char *bottom);
esp_err_t ui_oled_show_timer(const char *value, bool timer_seconds, unsigned position);
esp_err_t ui_oled_show_time_editor(const char *title, const char *value,
                                   const char *footer);
esp_err_t ui_oled_show_complete(const char *text);
esp_err_t ui_oled_show_info(unsigned voltage_v, unsigned ntc_c, unsigned igbt_c, bool valid);
esp_err_t ui_oled_show_sleep_clock(const char *text, unsigned y, unsigned alignment);
esp_err_t ui_oled_power(bool enabled);
esp_err_t ui_buzzer_chirp(unsigned frequency_hz, unsigned duration_ms);
esp_err_t ui_buzzer_chirp_duty(unsigned frequency_hz, unsigned duration_ms,
                               unsigned duty_permille);
void ui_buzzer_stop(void);
