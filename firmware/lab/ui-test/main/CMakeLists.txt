idf_component_register(
    SRCS
        "app_main.c"
        "telemetry.c"
        "ui_inputs.c"
        "ui_outputs.c"
        "powerboard_ro.c"
        "network.c"
        "web_server.c"
    INCLUDE_DIRS "."
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

# Optional per-build credentials are supplied as hex via environment variables.
# This keeps plaintext Wi-Fi credentials out of sdkconfig and the source tree.
if(DEFINED ENV{MCL02M_STA_SSID_HEX} AND NOT "$ENV{MCL02M_STA_SSID_HEX}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MCL02M_STA_SSID_HEX="$ENV{MCL02M_STA_SSID_HEX}"
        MCL02M_STA_PASSWORD_HEX="$ENV{MCL02M_STA_PASSWORD_HEX}"
    )
endif()
