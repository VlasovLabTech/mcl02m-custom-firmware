#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "network.h"
#include "powerboard_ro.h"
#include "safety.h"
#include "telemetry.h"
#include "ui_inputs.h"
#include "ui_outputs.h"
#include "web_server.h"

static const char *TAG = "mcl02m_ui_test";

void app_main(void)
{
    ESP_LOGW(TAG, "============================================================");
    ESP_LOGW(TAG, " MCL02M UI TEST — HEAT CONTROL COMPILED OUT");
    ESP_LOGW(TAG, " Inputs publish telemetry only. Power-board access is read-only.");
    ESP_LOGW(TAG, "============================================================");

    ESP_ERROR_CHECK(telemetry_init());
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"boot\","
                    "\"heat_control_enabled\":false,"
                    "\"powerboard_write_registers\":[]}", esp_timer_get_time() / 1000);

    ESP_ERROR_CHECK(ui_outputs_init());
    ui_outputs_all_off("boot");
    ESP_ERROR_CHECK(ui_inputs_init());
    const esp_err_t powerboard_err = powerboard_ro_start_monitor();
    if (powerboard_err != ESP_OK) {
        ESP_LOGW(TAG, "Read-only power-board monitor unavailable at boot: %s",
                 esp_err_to_name(powerboard_err));
    }
    const esp_err_t network_err = network_init();
    if (network_err == ESP_OK) {
        const esp_err_t web_err = web_server_start();
        if (web_err == ESP_OK) {
            ESP_LOGW(TAG, "Enter this token in the web page: %s", web_server_token());
        } else {
            ESP_LOGE(TAG, "Web server unavailable, UART telemetry remains active: %s",
                     esp_err_to_name(web_err));
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"web_error\",\"esp_err\":%d}",
                            esp_timer_get_time() / 1000, web_err);
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi unavailable, UART telemetry remains active: %s",
                 esp_err_to_name(network_err));
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"wifi_error\",\"esp_err\":%d}",
                        esp_timer_get_time() / 1000, network_err);
    }
    ESP_LOGW(TAG, "Power board is monitored read-only; no start/gear/heat writes exist.");
}
