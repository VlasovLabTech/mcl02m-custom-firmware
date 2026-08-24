#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "network.h"
#include "powerboard_control.h"
#include "safety.h"
#include "status_display.h"
#include "telemetry.h"
#include "ui_inputs.h"
#include "ui_outputs.h"
#include "web_server_power.h"

static const char *TAG = "mcl02m_power_test";

void app_main(void)
{
    ESP_LOGW(TAG, "============================================================");
    ESP_LOGW(TAG, " MCL02M POWER-STAGE BRING-UP — ACTIVE I2C TEST IMAGE");
    ESP_LOGW(TAG, " Boot state is STOP. Heating requires ARM + bounded START.");
    ESP_LOGW(TAG, "============================================================");

    ESP_ERROR_CHECK(telemetry_init());
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"boot\","
                    "\"firmware\":\"power_stage_bringup\","
                    "\"default_state\":\"STOP\",\"max_gear\":99,"
                    "\"max_run_ms\":300000,\"igbt_limit_c\":80,"
                    "\"bottom_limit_c\":120,\"heartbeat_ms\":%u,"
                    "\"start_confirm_timeout_ms\":%u}",
                    esp_timer_get_time() / 1000, MCL02M_CONTROL_HEARTBEAT_MS,
                    MCL02M_START_CONFIRM_TIMEOUT_MS);

    ESP_ERROR_CHECK(ui_outputs_init());
    ui_outputs_all_off("power_test_boot");
    ESP_ERROR_CHECK(ui_inputs_init());

    const esp_err_t power_err = powerboard_control_init();
    if (power_err != ESP_OK) {
        ESP_LOGE(TAG, "Power-board controller failed to initialize: %s",
                 esp_err_to_name(power_err));
    }
    ESP_ERROR_CHECK(status_display_start());

    const esp_err_t network_err = network_init();
    if (network_err == ESP_OK) {
        const esp_err_t web_err = web_server_power_start();
        if (web_err == ESP_OK) {
            ESP_LOGW(TAG, "Remote-control token: %s", web_server_power_token());
        } else {
            ESP_LOGE(TAG, "Web server unavailable: %s", esp_err_to_name(web_err));
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi unavailable; local STOP and OLED remain active: %s",
                 esp_err_to_name(network_err));
    }
}
