#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NOT_SUPPORTED 4
#define ESP_ERR_NO_MEM 5
#define ESP_ERR_INVALID_CRC 6
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
extern int64_t host_now_us;
static inline int64_t esp_timer_get_time(void) { return host_now_us; }
static inline size_t host_strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) { size_t copy = n < size - 1 ? n : size - 1; memcpy(dst, src, copy); dst[copy] = 0; }
    return n;
}
#define strlcpy host_strlcpy
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }
static inline QueueHandle_t xQueueCreate(unsigned n, unsigned size) { (void)n; (void)size; return (void *)1; }
static inline int xQueueSend(QueueHandle_t q, const void *v, TickType_t t) { (void)q; (void)v; (void)t; return 1; }
static inline int xQueueReceive(QueueHandle_t q, void *v, TickType_t t) { (void)q; (void)v; (void)t; return 0; }
static inline int xTaskCreate(void (*f)(void *), const char *n, unsigned s, void *a, unsigned p, TaskHandle_t *h) {
    (void)f; (void)n; (void)s; (void)a; (void)p; if (h) *h = NULL; return pdPASS;
}
static inline void xTaskNotifyGive(TaskHandle_t h) { (void)h; }
static inline TickType_t xTaskGetTickCount(void) { return (TickType_t)(host_now_us / 1000); }
static inline uint32_t ulTaskNotifyTake(int clear, TickType_t t) { (void)clear; host_now_us += (int64_t)t * 1000; return 0; }
static inline void vTaskDelay(TickType_t t) { host_now_us += (int64_t)t * 1000; }
static inline void vTaskDelayUntil(TickType_t *t, TickType_t d) { *t += d; host_now_us += (int64_t)d * 1000; }
static inline int esp_task_wdt_add(void *t) { (void)t; return 0; }
static inline int esp_task_wdt_reset(void) { return 0; }
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt; struct { bool enable_internal_pullup; } flags; } i2c_master_bus_config_t;
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0
static inline int i2c_new_master_bus(const i2c_master_bus_config_t *c, void **b) { (void)c; *b = (void *)1; return 0; }
static inline int i2c_master_bus_add_device(void *b, const i2c_device_config_t *c, void **d) { (void)b; (void)c; *d = (void *)1; return 0; }
static inline int i2c_del_master_bus(void *b) { (void)b; return 0; }
static inline int i2c_master_transmit(void *d, const uint8_t *b, size_t n, int t) { (void)d; (void)b; (void)n; (void)t; return 0; }
static inline int i2c_master_receive(void *d, uint8_t *b, size_t n, int t) { (void)d; (void)b; (void)n; (void)t; return ESP_ERR_TIMEOUT; }
