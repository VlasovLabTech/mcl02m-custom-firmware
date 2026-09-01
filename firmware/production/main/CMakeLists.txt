set(UI_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../lab/ui-test/main")
set(POWER_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../lab/power-test/main")
set(PRIVATE_SOUND_SRCS)
set(PRIVATE_SOUND_INCLUDE_DIRS)

if(MCL02M_PRIVATE_SOUND_BUILD)
    set(PRIVATE_SOUND_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../../assets/sounds/midi/generated/code")
    set(PRIVATE_SOUND_HEADER
        "${PRIVATE_SOUND_DIR}/melody_tables_midi.generated.h")
    if(NOT EXISTS "${PRIVATE_SOUND_HEADER}")
        message(FATAL_ERROR
            "Private sound build requires ${PRIVATE_SOUND_HEADER}")
    endif()
    list(APPEND PRIVATE_SOUND_SRCS "private_sound_tables.c")
    list(APPEND PRIVATE_SOUND_INCLUDE_DIRS "${PRIVATE_SOUND_DIR}")
endif()

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
        ${PRIVATE_SOUND_SRCS}
        "${POWER_SHARED}/powerboard_control.c"
        "${UI_SHARED}/telemetry.c"
        "${UI_SHARED}/ui_inputs.c"
        "${UI_SHARED}/ui_outputs.c"
    INCLUDE_DIRS
        "."
        "${POWER_SHARED}"
        "${UI_SHARED}"
        ${PRIVATE_SOUND_INCLUDE_DIRS}
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

if(MCL02M_PRIVATE_SOUND_BUILD)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MCL02M_PRIVATE_SOUND_BUILD=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MCL02M_PRIVATE_SOUND_BUILD=0)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MCL02M_POWER_TEST_BUILD=0
    MCL02M_MAX_RUN_MS=28800000U
    # Advisory at >92 C; two runtime samples above 98 C produce a separately
    # marked interface E07. A stopped cooker cannot Start above 80 C.
    MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED=1
    MCL02M_MAX_IGBT_C=98U
    MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES=2U
    MCL02M_IGBT_START_INHIBIT_C=80U
    # Stock-firmware static analysis shows 0x81/0x00/0x00 as the distinct
    # in-session zero-output command. Keep both switches here for quick lab
    # fallback after the first supervised hardware validation.
    MCL02M_ACTIVE_ZERO_ENABLED=1
    MCL02M_ACTIVE_ZERO_DIAGNOSTICS=1
    MCL02M_COMPACT_UART_TELEMETRY=1
    # The independent power task stops a live session if the cooking task no
    # longer renews its generation-tagged three-second lease.
    MCL02M_COOKING_LEASE_ENABLED=1
    # Separate interface-side emergency cutoff above the 190 C setpoint range.
    # The stock power MCU continues to enforce its native E05 protection.
    MCL02M_MAX_BOTTOM_C=210U
    MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES=6U
    MCL02M_RAW_SENSOR_FAULT_SAMPLES=2U
)
