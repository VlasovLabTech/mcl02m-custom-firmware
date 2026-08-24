set(UI_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../ui-test/main")

idf_component_register(
    SRCS
        "app_main.c"
        "powerboard_control.c"
        "status_display.c"
        "web_server_power.c"
        "${UI_SHARED}/telemetry.c"
        "${UI_SHARED}/ui_inputs.c"
        "${UI_SHARED}/ui_outputs.c"
        "${UI_SHARED}/network.c"
    INCLUDE_DIRS
        "."
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
        esp_timer
        esp_wifi
)

# Optional credentials are injected as hex for this local build and are never
# written to NVS or committed as plaintext.
if(DEFINED ENV{MCL02M_STA_SSID_HEX} AND NOT "$ENV{MCL02M_STA_SSID_HEX}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MCL02M_STA_SSID_HEX="$ENV{MCL02M_STA_SSID_HEX}"
        MCL02M_STA_PASSWORD_HEX="$ENV{MCL02M_STA_PASSWORD_HEX}"
    )
endif()
