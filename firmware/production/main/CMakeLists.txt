set(UI_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../lab/ui-test/main")
set(POWER_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../lab/power-test/main")

idf_component_register(
    SRCS
        "app_main.c"
        "cooking_engine.c"
        "display_prod.c"
        "indicators.c"
        "network_prod.c"
        "oled_assets.c"
        "settings.c"
        "sound.c"
        "temperature_ctrl.c"
        "ui_controller.c"
        "web_server_prod.c"
        "${POWER_SHARED}/powerboard_control.c"
        "${UI_SHARED}/telemetry.c"
        "${UI_SHARED}/ui_inputs.c"
        "${UI_SHARED}/ui_outputs.c"
    INCLUDE_DIRS
        "."
        "${POWER_SHARED}"
        "${UI_SHARED}"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_spi
        esp_driver_uart
        esp_event
        esp_http_server
        esp_netif
        esp_system
        esp_timer
        esp_wifi
        mbedtls
        nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MCL02M_POWER_TEST_BUILD=0
    MCL02M_MAX_RUN_MS=18000000U
    MCL02M_MAX_IGBT_C=80U
    MCL02M_MAX_BOTTOM_C=120U
)
