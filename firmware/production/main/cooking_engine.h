#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_types.h"
#include "esp_err.h"

esp_err_t cooking_engine_init(void);
void cooking_engine_get_snapshot(cooker_snapshot_t *snapshot);
size_t cooking_engine_status_json(char *output, size_t output_size);

esp_err_t cooking_set_mode(cook_mode_t mode);
esp_err_t cooking_set_power(unsigned gear);
esp_err_t cooking_set_temperature(unsigned temperature_c);
esp_err_t cooking_profile_select(unsigned index);
esp_err_t cooking_start(void);
esp_err_t cooking_stop(const char *reason);
esp_err_t cooking_pause_resume(void);
esp_err_t cooking_sleep(void);
esp_err_t cooking_wake(void);
esp_err_t cooking_acknowledge(void);
esp_err_t cooking_acknowledge_warning(void);

esp_err_t cooking_timer_set(uint32_t seconds);
esp_err_t cooking_timer_disable(void);
esp_err_t cooking_schedule_relative(uint32_t delay_s);
esp_err_t cooking_schedule_absolute(int64_t epoch_s);
esp_err_t cooking_schedule_cancel(void);

const char *cooking_state_name(cook_state_t state);
const char *cooking_fault_name(cooker_fault_t fault);
