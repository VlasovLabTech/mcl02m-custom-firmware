#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t reg;
    uint8_t value;
    uint8_t checksum;
    bool checksum_ok;
} powerboard_ro_value_t;

esp_err_t powerboard_ro_read(uint8_t reg, powerboard_ro_value_t *result);
esp_err_t powerboard_ro_snapshot_json(char *output, size_t output_size);
esp_err_t powerboard_ro_start_monitor(void);
