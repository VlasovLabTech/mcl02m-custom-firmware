#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "ui_outputs.h"

esp_err_t display_prod_init(void);
void display_prod_activity(void);
bool display_prod_is_awake(void);
void display_prod_show_confirm(void);
void display_prod_show_cancel(void);
void display_prod_set_overlay(const char *lines[UI_OLED_TEXT_LINES]);
void display_prod_set_menu_overlay(unsigned index, const char *label, const char *subtitle);
void display_prod_set_number_overlay(unsigned value);
void display_prod_set_focus_overlay(const char *top_left, const char *top_right,
                                    const char *value, bool degree, const char *bottom);
void display_prod_set_temperature_edit_overlay(unsigned setpoint_c, unsigned current_c,
                                               bool current_valid);
void display_prod_set_time_editor_overlay(const char *title, const char *value,
                                          const char *footer);
void display_prod_set_info_overlay(unsigned voltage_v, unsigned ntc_c, unsigned igbt_c, bool valid);
void display_prod_clear_overlay(void);
