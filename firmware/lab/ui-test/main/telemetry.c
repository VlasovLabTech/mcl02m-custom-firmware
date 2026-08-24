#include "telemetry.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct {
    char text[TELEMETRY_MESSAGE_MAX];
} telemetry_message_t;

static const char *TAG = "telemetry";
static QueueHandle_t s_queue;
static telemetry_sink_t s_sink;
static void *s_sink_ctx;
static portMUX_TYPE s_sink_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped;

static void telemetry_task(void *arg)
{
    (void)arg;
    telemetry_message_t message;

    for (;;) {
        if (xQueueReceive(s_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "%s", message.text);

        telemetry_sink_t sink;
        void *ctx;
        portENTER_CRITICAL(&s_sink_lock);
        sink = s_sink;
        ctx = s_sink_ctx;
        portEXIT_CRITICAL(&s_sink_lock);
        if (sink != NULL) {
            sink(message.text, ctx);
        }
    }
}

esp_err_t telemetry_init(void)
{
    s_queue = xQueueCreate(32, sizeof(telemetry_message_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void telemetry_set_sink(telemetry_sink_t sink, void *ctx)
{
    portENTER_CRITICAL(&s_sink_lock);
    s_sink = sink;
    s_sink_ctx = ctx;
    portEXIT_CRITICAL(&s_sink_lock);
}

void telemetry_emit(const char *json)
{
    if (s_queue == NULL || json == NULL) {
        return;
    }

    telemetry_message_t message = {0};
    strlcpy(message.text, json, sizeof(message.text));
    if (xQueueSend(s_queue, &message, 0) != pdTRUE) {
        __atomic_add_fetch(&s_dropped, 1, __ATOMIC_RELAXED);
    }
}

void telemetry_emitf(const char *format, ...)
{
    telemetry_message_t message = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(message.text, sizeof(message.text), format, args);
    va_end(args);
    telemetry_emit(message.text);
}

uint32_t telemetry_dropped_count(void)
{
    return __atomic_load_n(&s_dropped, __ATOMIC_RELAXED);
}
