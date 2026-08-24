#pragma once

#include <stdbool.h>

#include "app_types.h"
#include "esp_err.h"

esp_err_t settings_init(void);
void settings_get(app_settings_t *settings);
esp_err_t settings_update(const app_settings_t *settings);
bool settings_persistence_available(void);

bool settings_wifi_get(char *ssid, size_t ssid_size, char *password, size_t password_size);
esp_err_t settings_wifi_set(const char *ssid, const char *password);
esp_err_t settings_wifi_clear(void);
esp_err_t settings_factory_reset(void);

bool settings_admin_is_configured(void);
bool settings_admin_verify(const char *password);
esp_err_t settings_admin_set(const char *password);

void settings_profiles_get(cooker_profile_t profiles[COOKER_PROFILE_COUNT]);
esp_err_t settings_profile_set(unsigned index, const cooker_profile_t *profile);
unsigned settings_profile_stage_count(const cooker_profile_t *profile);
uint32_t settings_profile_total_s(const cooker_profile_t *profile);
