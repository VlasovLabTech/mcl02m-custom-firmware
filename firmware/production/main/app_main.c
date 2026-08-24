#include "esp_err.h"
#include "esp_log.h"

#include "app_config.h"
#include "cooking_engine.h"
#include "display_prod.h"
#include "indicators.h"
#include "network_prod.h"
#include "settings.h"
#include "sound.h"
#include "telemetry.h"
#include "ui_controller.h"
#include "ui_inputs.h"
#include "ui_outputs.h"
#include "web_server_prod.h"

static const char *TAG = "mcl02m_custom";

void app_main(void)
{
    ESP_LOGW(TAG, "MCL02M custom %s: boot output is STOP", MCL02M_FIRMWARE_VERSION);
    ESP_ERROR_CHECK(telemetry_init());
    ESP_ERROR_CHECK(ui_outputs_init());
    ui_outputs_all_off("production_boot");
    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(sound_init());

    app_settings_t settings;
    settings_get(&settings);
    sound_set_enabled(settings.sound_enabled);

    ESP_ERROR_CHECK(ui_inputs_init());
    ESP_ERROR_CHECK(cooking_engine_init());
    ESP_ERROR_CHECK(display_prod_init());
    sound_play(SOUND_BOOT);
    ESP_ERROR_CHECK(ui_controller_init());

    const esp_err_t network_err = network_prod_init();
    if (network_err != ESP_OK) {
        ESP_LOGE(TAG, "Network disabled: %s; local cooking remains available",
                 esp_err_to_name(network_err));
    } else {
        const esp_err_t web_err = web_server_prod_start();
        if (web_err != ESP_OK) ESP_LOGE(TAG, "Web disabled: %s", esp_err_to_name(web_err));
    }
    ESP_ERROR_CHECK(indicators_init());
}
