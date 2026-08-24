#pragma once

#include "esp_err.h"

esp_err_t network_init(void);
esp_err_t network_connect_sta_ram(const char *ssid, const char *password);
const char *network_ap_ssid(void);
