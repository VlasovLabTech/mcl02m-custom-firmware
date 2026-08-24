#include "status_display.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "powerboard_control.h"
#include "ui_outputs.h"

static const char *short_state(powerboard_state_t state)
{
    switch (state) {
    case PB_STATE_BOOT: return "PWR BOOT";
    case PB_STATE_STOPPED: return "SAFE STOP";
    case PB_STATE_ARMED: return "ARMED";
    case PB_STATE_STARTING: return "STARTING";
    case PB_STATE_HEATING: return "HEATING";
    case PB_STATE_PAUSED: return "PAUSED";
    case PB_STATE_NO_PAN: return "NO PAN";
    case PB_STATE_HEARTBEAT_GAP: return "HB GAP";
    case PB_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

static const char *topology_text(uint8_t topology)
{
    switch (topology) {
    case 0xa1: return "A";
    case 0xc1: return "B";
    case 0xe1: return "AB";
    case 0x80: return "NP";
    default: return "--";
    }
}

static void display_task(void *arg)
{
    (void)arg;
    for (;;) {
        powerboard_status_t status;
        powerboard_control_get_status(&status);

        char text[UI_OLED_TEXT_LINES][UI_OLED_TEXT_COLUMNS + 1] = {{0}};
        const bool r26_valid = (status.valid_mask & (1U << 6)) != 0;
        const char *state_text = short_state(status.state);
        if (status.state == PB_STATE_HEATING && r26_valid && status.registers[6] == 0) {
            state_text = "PWR WAIT";
        }
        snprintf(text[0], sizeof(text[0]), "%-10.10s", state_text);
        snprintf(text[1], sizeof(text[1]), "G%02u>%02u%s", status.applied_gear,
                 status.target_gear, topology_text(status.topology));
        if ((status.valid_mask & (1U << 3)) != 0)
            snprintf(text[2], sizeof(text[2]), "IG:%03uC", status.igbt_c);
        else
            snprintf(text[2], sizeof(text[2]), "IG:---C");
        if ((status.valid_mask & (1U << 4)) != 0)
            snprintf(text[3], sizeof(text[3]), "BT:%03uC", status.bottom_c);
        else
            snprintf(text[3], sizeof(text[3]), "BT:---C");
        if ((status.valid_mask & ((1U << 0) | (1U << 6))) == ((1U << 0) | (1U << 6)))
            snprintf(text[4], sizeof(text[4]), "20%02X 26%02X", status.registers[0],
                     status.registers[6]);
        else
            snprintf(text[4], sizeof(text[4]), "20:-- 26:-");

        if (status.state == PB_STATE_ARMED) {
            const unsigned arm_s_raw = (unsigned)(status.arm_remaining_ms / 1000U);
            const unsigned arm_s = arm_s_raw > 99U ? 99U : arm_s_raw;
            snprintf(text[5], sizeof(text[5]), "ARM:%02uS", arm_s);
        } else if (status.state == PB_STATE_HEARTBEAT_GAP) {
            const unsigned gap_ms = status.heartbeat_gap_remaining_ms > 9999U ?
                                    9999U : (unsigned)status.heartbeat_gap_remaining_ms;
            snprintf(text[5], sizeof(text[5]), "GAP:%04u", gap_ms);
        } else if (status.state == PB_STATE_STARTING) {
            const unsigned wait_ds_raw = (unsigned)(status.start_confirm_remaining_ms / 100U);
            const unsigned wait_ds = wait_ds_raw > 99U ? 99U : wait_ds_raw;
            snprintf(text[5], sizeof(text[5]), "PWR:%u.%uS", wait_ds / 10U, wait_ds % 10U);
        } else if (status.state == PB_STATE_FAULT) {
            snprintf(text[5], sizeof(text[5]), "E:%.8s", status.fault);
        } else if (status.run_remaining_ms != 0) {
            const unsigned run_s_raw = (unsigned)(status.run_remaining_ms / 1000U);
            const unsigned run_s = run_s_raw > 999U ? 999U : run_s_raw;
            snprintf(text[5], sizeof(text[5]), "RUN:%03uS", run_s);
        } else {
            snprintf(text[5], sizeof(text[5]), "E:%.8s", status.fault);
        }

        const char *lines[UI_OLED_TEXT_LINES];
        for (unsigned i = 0; i < UI_OLED_TEXT_LINES; ++i) lines[i] = text[i];
        ui_oled_show_text(lines);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t status_display_start(void)
{
    return xTaskCreate(display_task, "status_oled", 4096, NULL, 4, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}
