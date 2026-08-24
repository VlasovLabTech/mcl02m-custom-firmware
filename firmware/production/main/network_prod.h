#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    bool enabled;
    bool sta_connected;
    bool ap_ready;
    bool clock_synchronized;
    char sta_ip[16];
    char ap_ssid[33];
} network_status_t;

esp_err_t network_prod_init(void);
esp_err_t network_prod_set_enabled(bool enabled);
esp_err_t network_prod_configure(const char *ssid, const char *password);
const char *network_prod_setup_password(void);
void network_prod_apply_timezone(void);
void network_prod_get_status(network_status_t *status);
