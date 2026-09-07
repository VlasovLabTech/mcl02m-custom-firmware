#include "power_sweep_experiment.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "cooking_engine.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "powerboard_control.h"

#define SWEEP_DWELL_MS 15000U
#define SWEEP_TRANSITION_TIMEOUT_MS 15000U
#define SWEEP_STOP_TIMEOUT_MS 12000U
#define SWEEP_HOST_TIMEOUT_MS 5000U
#define SWEEP_LABEL_MAX 24U

typedef struct {
    char label[SWEEP_LABEL_MAX];
} sweep_request_t;

static const uint8_t s_points[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 25, 30,
    35, 36, 40, 45, 50, 55, 56, 60, 70, 80, 90, 99,
};

static QueueHandle_t s_requests;
static volatile bool s_running;
static volatile bool s_abort_requested;
static volatile int64_t s_last_host_contact_us;

static void frame(const char *event, const char *detail)
{
    printf("X,%s,%s\n", event, detail == NULL ? "" : detail);
    fflush(stdout);
}

static bool label_valid(const char *label)
{
    const size_t length = strlen(label);
    if (length == 0 || length >= SWEEP_LABEL_MAX) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)label[i];
        if (!isalnum(c) && c != '-' && c != '_') return false;
    }
    return true;
}

static bool interrupted(const char **reason)
{
    if (s_abort_requested) {
        *reason = "HOST_ABORT";
        return true;
    }
    if (s_running && esp_timer_get_time() - s_last_host_contact_us >=
                     (int64_t)SWEEP_HOST_TIMEOUT_MS * 1000) {
        *reason = "HOST_LOST";
        return true;
    }
    cooker_snapshot_t cooker;
    powerboard_status_t power;
    cooking_engine_get_snapshot(&cooker);
    powerboard_control_get_status(&power);
    if (cooker.state == COOK_STATE_FAULT || power.state == PB_STATE_FAULT) {
        *reason = "FAULT";
        return true;
    }
    if (cooker.state == COOK_STATE_NO_PAN || power.state == PB_STATE_NO_PAN) {
        *reason = "NO_PAN";
        return true;
    }
    if (s_running && (cooker.state == COOK_STATE_STOPPING ||
                      cooker.state == COOK_STATE_IDLE ||
                      cooker.state == COOK_STATE_SLEEP)) {
        *reason = "PHYSICAL_STOP";
        return true;
    }
    return false;
}

static bool wait_for_idle(uint32_t timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        cooker_snapshot_t cooker;
        powerboard_status_t power;
        cooking_engine_get_snapshot(&cooker);
        powerboard_control_get_status(&power);
        if ((cooker.state == COOK_STATE_IDLE || cooker.state == COOK_STATE_READY ||
             cooker.state == COOK_STATE_COMPLETE) &&
            power.state == PB_STATE_STOPPED && power.stop_verified)
            return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool wait_for_power(uint8_t requested, bool limited,
                           uint32_t timeout_ms, const char **reason)
{
    const uint8_t actual = limited && requested > COOKER_HOLD_MAX_GEAR ?
                           COOKER_HOLD_MAX_GEAR : requested;
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        if (interrupted(reason)) return false;
        cooker_snapshot_t cooker;
        powerboard_status_t power;
        cooking_engine_get_snapshot(&cooker);
        powerboard_control_get_status(&power);
        const bool settled = !power.transition_pending &&
            power.last_command_0c == actual && power.transmitted_gear == actual;
        if (requested == 0) {
            if (cooker.state == COOK_STATE_COOKING && cooker.active_zero &&
                power.state == PB_STATE_ACTIVE_ZERO && settled &&
                power.last_command_0d == 0x81 && power.last_command_00 == 0)
                return true;
        } else if (cooker.state == COOK_STATE_COOKING &&
                   power.state == PB_STATE_HEATING &&
                   power.applied_gear == actual && settled &&
                   power.last_command_00 == 1) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    *reason = "SETTLE_TIMEOUT";
    return false;
}

static bool output_still_matches(uint8_t requested, bool limited)
{
    const uint8_t actual = limited && requested > COOKER_HOLD_MAX_GEAR ?
                           COOKER_HOLD_MAX_GEAR : requested;
    cooker_snapshot_t cooker;
    powerboard_status_t power;
    cooking_engine_get_snapshot(&cooker);
    powerboard_control_get_status(&power);
    const bool command_matches = !power.transition_pending &&
        power.target_gear == actual && power.applied_gear == actual &&
        power.transmitted_gear == actual && power.last_command_0c == actual;
    if (requested == 0) {
        return cooker.state == COOK_STATE_COOKING && cooker.active_zero &&
               power.state == PB_STATE_ACTIVE_ZERO && command_matches &&
               power.last_command_0d == 0x81 && power.last_command_00 == 0;
    }
    return cooker.state == COOK_STATE_COOKING &&
           power.state == PB_STATE_HEATING && command_matches &&
           power.last_command_00 == 1;
}

static bool dwell(uint8_t requested, bool limited, const char **reason)
{
    const uint8_t actual = limited && requested > COOKER_HOLD_MAX_GEAR ?
                           COOKER_HOLD_MAX_GEAR : requested;
    char payload[64];
    snprintf(payload, sizeof(payload), "%u,BEGIN,%u,%u", requested, actual,
             limited ? 1U : 0U);
    frame("P", payload);
    const int64_t deadline = esp_timer_get_time() + (int64_t)SWEEP_DWELL_MS * 1000;
    while (esp_timer_get_time() < deadline) {
        if (interrupted(reason)) return false;
        /* A rotary edit or any other local action must not silently corrupt a
         * labelled dwell.  Abort the run and let safe_stop() own the output. */
        if (!output_still_matches(requested, limited)) {
            *reason = "OUTPUT_CHANGED";
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    snprintf(payload, sizeof(payload), "%u,END,%u,%u", requested, actual,
             limited ? 1U : 0U);
    frame("P", payload);
    return true;
}

static void safe_stop(void)
{
    (void)cooking_stop("SWEEP STOP");
    (void)wait_for_idle(SWEEP_STOP_TIMEOUT_MS);
}

static void run_sweep(const char *label)
{
    const char *reason = "NONE";
    char payload[96];
    s_abort_requested = false;
    s_last_host_contact_us = esp_timer_get_time();
    s_running = true;
    snprintf(payload, sizeof(payload), "%s,%u,%u", label,
             (unsigned)(sizeof(s_points) / sizeof(s_points[0])), SWEEP_DWELL_MS);
    frame("BEGIN", payload);

    cooker_snapshot_t cooker;
    cooking_engine_get_snapshot(&cooker);
    if (cooker.state == COOK_STATE_SLEEP) {
        (void)cooking_wake();
        vTaskDelay(pdMS_TO_TICKS(250));
    } else if (cooker.state == COOK_STATE_COMPLETE) {
        (void)cooking_acknowledge();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (!wait_for_idle(3000) || cooking_set_mode(COOK_MODE_POWER) != ESP_OK ||
        cooking_set_power(1) != ESP_OK || cooking_start() != ESP_OK) {
        reason = "START_REQUEST";
        goto done;
    }
    if (!wait_for_power(1, false, SWEEP_TRANSITION_TIMEOUT_MS, &reason)) goto done;

    for (size_t i = 0; i < sizeof(s_points) / sizeof(s_points[0]); ++i) {
        const uint8_t requested = s_points[i];
        powerboard_status_t power;
        powerboard_control_get_status(&power);
        const bool limited = power.cookware_limited &&
                             requested > COOKER_HOLD_MAX_GEAR;
        snprintf(payload, sizeof(payload), "%u,SET", requested);
        frame("P", payload);
        const esp_err_t set = cooking_set_power(requested);
        if (set != ESP_OK && !limited) {
            reason = "SET_REQUEST";
            goto done;
        }
        if (!wait_for_power(requested, limited,
                            SWEEP_TRANSITION_TIMEOUT_MS, &reason)) goto done;
        if (!dwell(requested, limited, &reason)) goto done;
        if (requested == COOKER_HOLD_MAX_GEAR) {
            powerboard_control_get_status(&power);
            if (power.cookware_limited) {
                reason = "COMPLETE_LIMITED";
                goto done;
            }
        }
    }

    reason = "COMPLETE";
done:
    safe_stop();
    snprintf(payload, sizeof(payload), "%s,%s", label, reason);
    const bool completed = strcmp(reason, "COMPLETE") == 0 ||
                           strcmp(reason, "COMPLETE_LIMITED") == 0;
    frame(completed ? "DONE" : "ABORTED", payload);
    s_running = false;
    s_abort_requested = false;
}

static void sweep_task(void *arg)
{
    (void)arg;
    sweep_request_t request;
    for (;;) {
        if (xQueueReceive(s_requests, &request, portMAX_DELAY) == pdTRUE)
            run_sweep(request.label);
    }
}

static void handle_line(char *line)
{
    while (*line && (line[strlen(line) - 1] == '\r' ||
                     line[strlen(line) - 1] == '\n'))
        line[strlen(line) - 1] = 0;
    if (strcmp(line, "X,PING") == 0) {
        s_last_host_contact_us = esp_timer_get_time();
        frame("PONG", s_running ? "BUSY" : "READY");
        return;
    }
    if (strcmp(line, "X,ABORT") == 0) {
        s_last_host_contact_us = esp_timer_get_time();
        s_abort_requested = true;
        frame("ACK", "ABORT");
        return;
    }
    if (strncmp(line, "X,START,", 8) == 0) {
        s_last_host_contact_us = esp_timer_get_time();
        const char *label = line + 8;
        if (!label_valid(label)) {
            frame("REJECT", "LABEL");
            return;
        }
        if (s_running) {
            frame("REJECT", "BUSY");
            return;
        }
        sweep_request_t request = {0};
        strlcpy(request.label, label, sizeof(request.label));
        if (xQueueSend(s_requests, &request, 0) != pdTRUE) {
            frame("REJECT", "QUEUE");
            return;
        }
        frame("ACK", "START");
        return;
    }
    if (*line) frame("REJECT", "COMMAND");
}

static void uart_reader_task(void *arg)
{
    (void)arg;
    char line[64] = {0};
    size_t used = 0;
    for (;;) {
        char c;
        const ssize_t count = read(STDIN_FILENO, &c, 1);
        if (count != 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (used != 0) {
                line[used] = 0;
                handle_line(line);
                used = 0;
            }
        } else if (used + 1 < sizeof(line)) {
            line[used++] = c;
        } else {
            used = 0;
            frame("REJECT", "LINE");
        }
    }
}

esp_err_t power_sweep_experiment_init(void)
{
    s_requests = xQueueCreate(1, sizeof(sweep_request_t));
    if (s_requests == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sweep_task, "power_sweep", 5120, NULL, 6, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(uart_reader_task, "sweep_uart", 3072, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    char payload[80];
    snprintf(payload, sizeof(payload), "%s,%u,%u", MCL02M_FIRMWARE_VERSION,
             (unsigned)(sizeof(s_points) / sizeof(s_points[0])), SWEEP_DWELL_MS);
    frame("READY", payload);
    return ESP_OK;
}
