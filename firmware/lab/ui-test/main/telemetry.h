#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define TELEMETRY_MESSAGE_MAX 512

typedef void (*telemetry_sink_t)(const char *json, void *ctx);

esp_err_t telemetry_init(void);
void telemetry_set_sink(telemetry_sink_t sink, void *ctx);
void telemetry_emit(const char *json);
void telemetry_emitf(const char *format, ...) __attribute__((format(printf, 1, 2)));
uint32_t telemetry_dropped_count(void);
