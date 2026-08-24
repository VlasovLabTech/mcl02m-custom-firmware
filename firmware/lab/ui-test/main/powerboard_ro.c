#include "powerboard_ro.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "pins.h"
#include "safety.h"
#include "telemetry.h"

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_monitor_task;

/* Byte-for-byte tables used by stock 2.2.0_0016 for power-board type > 2. */
static const uint8_t s_bottom_ntc_lut_0b_fb[] = {
    0xf8, 0xf2, 0xeb, 0xe5, 0xe0, 0xdc, 0xd8, 0xd5, 0xd1, 0xce, 0xcb, 0xc8, 0xc5, 0xc3, 0xc1, 0xbf,
    0xbd, 0xbc, 0xba, 0xb9, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xaf, 0xae, 0xad, 0xac, 0xab, 0xaa,
    0xa9, 0xa8, 0xa7, 0xa6, 0xa5, 0xa4, 0xa3, 0xa2, 0xa1, 0xa0, 0x9f, 0x9e, 0x9d, 0x9c, 0x9b, 0x9a,
    0x99, 0x98, 0x97, 0x96, 0x95, 0x94, 0x93, 0x93, 0x92, 0x91, 0x90, 0x90, 0x8f, 0x8e, 0x8d, 0x8c,
    0x8c, 0x8b, 0x8a, 0x8a, 0x89, 0x89, 0x88, 0x88, 0x87, 0x86, 0x85, 0x85, 0x84, 0x83, 0x83, 0x82,
    0x81, 0x81, 0x80, 0x80, 0x7f, 0x7e, 0x7e, 0x7d, 0x7d, 0x7c, 0x7c, 0x7b, 0x7b, 0x7a, 0x7a, 0x79,
    0x79, 0x78, 0x78, 0x77, 0x77, 0x76, 0x76, 0x75, 0x75, 0x74, 0x74, 0x73, 0x73, 0x72, 0x71, 0x71,
    0x70, 0x6f, 0x6f, 0x6e, 0x6d, 0x6d, 0x6c, 0x6c, 0x6b, 0x6b, 0x6a, 0x6a, 0x69, 0x69, 0x68, 0x68,
    0x67, 0x67, 0x66, 0x66, 0x65, 0x65, 0x64, 0x64, 0x63, 0x63, 0x62, 0x62, 0x61, 0x61, 0x60, 0x60,
    0x5f, 0x5f, 0x5e, 0x5e, 0x5d, 0x5d, 0x5c, 0x5c, 0x5b, 0x5b, 0x5a, 0x5a, 0x59, 0x58, 0x58, 0x57,
    0x56, 0x56, 0x55, 0x55, 0x54, 0x53, 0x53, 0x52, 0x51, 0x51, 0x50, 0x50, 0x4f, 0x4e, 0x4e, 0x4d,
    0x4c, 0x4c, 0x4b, 0x4a, 0x49, 0x49, 0x48, 0x48, 0x47, 0x46, 0x46, 0x45, 0x45, 0x44, 0x44, 0x43,
    0x43, 0x42, 0x42, 0x41, 0x41, 0x40, 0x40, 0x3f, 0x3f, 0x3e, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b, 0x3b,
    0x3a, 0x39, 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30, 0x2f, 0x2e, 0x2d, 0x2c, 0x2b,
    0x2a, 0x29, 0x27, 0x25, 0x23, 0x21, 0x1f, 0x1c, 0x19, 0x16, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03,
    0x00,
};

static const uint8_t s_igbt_ntc_lut_41_f7[] = {
    0x7d, 0x7c, 0x7b, 0x7b, 0x7a, 0x79, 0x78, 0x78, 0x77, 0x76, 0x76, 0x75, 0x74, 0x74, 0x73, 0x72,
    0x72, 0x71, 0x70, 0x70, 0x6f, 0x6e, 0x6e, 0x6d, 0x6d, 0x6c, 0x6b, 0x6b, 0x6a, 0x6a, 0x69, 0x68,
    0x68, 0x67, 0x67, 0x66, 0x66, 0x65, 0x64, 0x64, 0x63, 0x63, 0x62, 0x62, 0x61, 0x61, 0x60, 0x60,
    0x5f, 0x5f, 0x5e, 0x5d, 0x5d, 0x5c, 0x5c, 0x5b, 0x5b, 0x5a, 0x5a, 0x59, 0x59, 0x58, 0x58, 0x57,
    0x57, 0x56, 0x56, 0x55, 0x55, 0x54, 0x54, 0x53, 0x53, 0x52, 0x52, 0x51, 0x51, 0x50, 0x50, 0x4f,
    0x4f, 0x4e, 0x4e, 0x4e, 0x4d, 0x4d, 0x4c, 0x4c, 0x4b, 0x4b, 0x4a, 0x4a, 0x49, 0x49, 0x48, 0x48,
    0x47, 0x47, 0x46, 0x46, 0x45, 0x45, 0x44, 0x44, 0x43, 0x43, 0x42, 0x41, 0x41, 0x40, 0x40, 0x3f,
    0x3f, 0x3e, 0x3e, 0x3d, 0x3d, 0x3c, 0x3c, 0x3b, 0x3b, 0x3a, 0x39, 0x39, 0x38, 0x38, 0x37, 0x37,
    0x36, 0x35, 0x35, 0x34, 0x34, 0x33, 0x32, 0x32, 0x31, 0x31, 0x30, 0x2f, 0x2f, 0x2e, 0x2d, 0x2d,
    0x2c, 0x2b, 0x2b, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x25, 0x24, 0x24, 0x23, 0x22, 0x21, 0x20,
    0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x12, 0x11, 0x10, 0x0e,
    0x0c, 0x0b, 0x09, 0x07, 0x05, 0x03, 0x00,
};

_Static_assert(sizeof(s_bottom_ntc_lut_0b_fb) == 0xfb - 0x0b + 1,
               "bottom NTC lookup length mismatch");
_Static_assert(sizeof(s_igbt_ntc_lut_41_f7) == 0xf7 - 0x41 + 1,
               "IGBT NTC lookup length mismatch");

static uint8_t stock_bottom_temperature(uint8_t raw)
{
    if (raw < 0x0b) return 0xfa;
    if (raw >= 0xfc) return 0;
    return s_bottom_ntc_lut_0b_fb[raw - 0x0b];
}

static uint8_t stock_igbt_temperature(uint8_t raw)
{
    if (raw < 0x41) return 0x7d;
    if (raw >= 0xf8) return 0;
    return s_igbt_ntc_lut_41_f7[raw - 0x41];
}

static esp_err_t ensure_initialized(void)
{
    if (s_device != NULL) {
        return ESP_OK;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_POWERBOARD_SDA,
        .scl_io_num = PIN_POWERBOARD_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x2a,
        .scl_speed_hz = 10000,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    return err;
}

esp_err_t powerboard_ro_read(uint8_t reg, powerboard_ro_value_t *result)
{
    if (result == NULL || !mcl02m_powerboard_read_selector_allowed(reg)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_initialized();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t response[2] = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);

    /*
     * This is the only power-board write transaction in the entire test app:
     * one whitelisted selector byte 0x20..0x2f. No value byte follows it.
     */
    err = i2c_master_transmit(s_device, &reg, 1, 50);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        err = i2c_master_receive(s_device, response, sizeof(response), 50);
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        return err;
    }

    result->reg = reg;
    result->value = response[0];
    result->checksum = response[1];
    result->checksum_ok = response[1] == (uint8_t)(response[0] + reg);
    return result->checksum_ok ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t powerboard_ro_snapshot_json(char *output, size_t output_size)
{
    if (output == NULL || output_size < 128) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t used = (size_t)snprintf(output, output_size,
        "{\"t_ms\":%lld,\"type\":\"powerboard_ro\",\"registers\":{",
        esp_timer_get_time() / 1000);
    esp_err_t aggregate = ESP_OK;
    uint8_t values[8] = {0};
    bool valid[8] = {false};

    for (uint8_t reg = 0x20; reg <= 0x27; ++reg) {
        powerboard_ro_value_t value = {0};
        const esp_err_t err = powerboard_ro_read(reg, &value);
        values[reg - 0x20] = value.value;
        valid[reg - 0x20] = err == ESP_OK;
        if (err != ESP_OK && aggregate == ESP_OK) {
            aggregate = err;
        }
        if (used < output_size) {
            used += (size_t)snprintf(output + used, output_size - used,
                "%s\"%02X\":{\"value\":%u,\"checksum\":%u,\"ok\":%s,\"err\":%d}",
                reg == 0x20 ? "" : ",", reg, value.value, value.checksum,
                value.checksum_ok ? "true" : "false", err);
        }
    }

    if (used < output_size) {
        char igbt_raw[16];
        char igbt_c[16];
        char bottom_raw[16];
        char bottom_c[16];
        if (valid[3]) {
            snprintf(igbt_raw, sizeof(igbt_raw), "%u", values[3]);
            snprintf(igbt_c, sizeof(igbt_c), "%u", stock_igbt_temperature(values[3]));
        } else {
            strlcpy(igbt_raw, "null", sizeof(igbt_raw));
            strlcpy(igbt_c, "null", sizeof(igbt_c));
        }
        if (valid[4]) {
            snprintf(bottom_raw, sizeof(bottom_raw), "%u", values[4]);
            snprintf(bottom_c, sizeof(bottom_c), "%u", stock_bottom_temperature(values[4]));
        } else {
            strlcpy(bottom_raw, "null", sizeof(bottom_raw));
            strlcpy(bottom_c, "null", sizeof(bottom_c));
        }
        snprintf(output + used, output_size - used,
            "},\"igbt_r23_raw\":%s,\"igbt_c\":%s,"
            "\"bottom_r24_raw\":%s,\"bottom_c\":%s,"
            "\"temperature_conversion\":\"stock 2.2.0_0016 lookup tables\","
            "\"result\":%d}",
            igbt_raw, igbt_c, bottom_raw, bottom_c,
            aggregate);
    }
    telemetry_emit(output);
    return aggregate;
}

static esp_err_t startup_probe(void)
{
    /* Exact read-only order used by FUN_40153b7c in the stock application. */
    powerboard_ro_value_t r25 = {0};
    powerboard_ro_value_t r28 = {0};
    powerboard_ro_value_t r29 = {0};
    powerboard_ro_value_t r24 = {0};
    powerboard_ro_value_t r2a = {0};
    powerboard_ro_value_t r2b = {0};

    esp_err_t aggregate = powerboard_ro_read(0x25, &r25);
    esp_err_t e28 = ESP_FAIL;
    esp_err_t e29 = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        e28 = powerboard_ro_read(0x28, &r28);
        e29 = powerboard_ro_read(0x29, &r29);
        if (e28 == ESP_OK && e29 == ESP_OK) break;
    }
    if (aggregate == ESP_OK && (e28 != ESP_OK || e29 != ESP_OK))
        aggregate = e28 != ESP_OK ? e28 : e29;
    const esp_err_t e24 = powerboard_ro_read(0x24, &r24);
    if (aggregate == ESP_OK && e24 != ESP_OK) aggregate = e24;
    esp_err_t e2a = ESP_FAIL;
    esp_err_t e2b = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        e2a = powerboard_ro_read(0x2a, &r2a);
        e2b = powerboard_ro_read(0x2b, &r2b);
        if (e2a == ESP_OK && e2b == ESP_OK) break;
    }
    if (aggregate == ESP_OK && (e2a != ESP_OK || e2b != ESP_OK))
        aggregate = e2a != ESP_OK ? e2a : e2b;

    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"powerboard_startup\","
                    "\"read_only\":true,\"r25\":%u,\"r28\":%u,\"r29\":%u,"
                    "\"r24\":%u,\"r2a\":%u,\"r2b\":%u,\"result\":%d}",
                    esp_timer_get_time() / 1000,
                    r25.value, r28.value, r29.value, r24.value,
                    r2a.value, r2b.value, aggregate);
    return aggregate;
}

static void monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        char snapshot[2048] = {0};
        const esp_err_t err = powerboard_ro_snapshot_json(snapshot, sizeof(snapshot));
        if (err != ESP_OK) {
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"powerboard_monitor_error\","
                            "\"esp_err\":%d}", esp_timer_get_time() / 1000, err);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t powerboard_ro_start_monitor(void)
{
    const esp_err_t probe_err = startup_probe();
    if (probe_err != ESP_OK) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"powerboard_startup_error\","
                        "\"esp_err\":%d}", esp_timer_get_time() / 1000, probe_err);
    }
    if (s_monitor_task != NULL) return probe_err;
    if (xTaskCreate(monitor_task, "powerboard_ro", 4096, NULL, 4, &s_monitor_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    /* A missing board is non-fatal: the background task keeps retrying read-only. */
    return ESP_OK;
}
