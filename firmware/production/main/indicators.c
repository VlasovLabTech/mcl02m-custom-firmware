#include "indicators.h"

#include "app_types.h"
#include "cooking_engine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_prod.h"
#include "pins.h"
#include "ui_outputs.h"
#include "ui_controller.h"

#ifndef MCL02M_ACTIVE_ZERO_DIAGNOSTICS
#define MCL02M_ACTIVE_ZERO_DIAGNOSTICS 0
#endif

#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
static const char *TAG = "leddbg";
#endif

static unsigned gear_to_leds(unsigned gear)
{
    if (gear == 0) return 0;
    unsigned level = (gear + 10U) / 11U;
    return level > 9 ? 9 : level;
}

static void indicators_task(void *arg)
{
    (void)arg;
    const TickType_t self_test_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    unsigned previous_leds = 10U;
    int previous_orange = -1;
    int previous_blue = -1;
    int previous_timer = -1;
#endif
    for (;;) {
        cooker_snapshot_t cooker;
        network_status_t network = {0};
        cooking_engine_get_snapshot(&cooker);
        network_prod_get_status(&network);
        const uint32_t phase = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        unsigned leds = 0;
        if ((int32_t)(self_test_deadline - xTaskGetTickCount()) > 0) {
            /* All nine white LEDs must be visibly confirmed after every boot. */
            leds = 9;
        } else if (cooker.state == COOK_STATE_READY && ui_controller_setpoint_editing()) {
            if ((phase % 1000U) < 700U) leds = gear_to_leds(cooker.selected_gear);
        } else if (cooker.state == COOK_STATE_PAUSED) {
            if ((phase % 2000U) < 1400U) leds = gear_to_leds(cooker.paused_gear);
        } else if (cooker.state == COOK_STATE_STARTING ||
                   cooker.state == COOK_STATE_COOKING ||
                   cooker.state == COOK_STATE_NO_PAN) {
            leds = gear_to_leds(cooker.applied_gear);
        }

        bool orange = false;
        bool blue = false;
        if (cooker.state == COOK_STATE_FAULT) orange = (phase % 500U) < 250U;
        else if (cooker.state == COOK_STATE_NO_PAN) orange = (phase % 1000U) < 500U;
        else if (cooker.hold_saturated) orange = true;
        else if (!network.enabled) blue = false;
        else if (network.sta_connected) blue = true;
        else blue = (phase % 2000U) < 500U;
        const esp_err_t panel_err = ui_led_panel_set(leds, orange, blue);

        bool timer_led = false;
        if (cooker.delayed_start) timer_led = (phase % 1000U) < 700U;
        else timer_led = cooker.timer_enabled || ui_controller_timer_editing();
        const esp_err_t timer_err = ui_direct_output_set(PIN_UI_DIRECT_0,
                                                         timer_led ? 1 : 0);
        /* GPIO32 had no observable panel effect and remains inactive. */
        ui_direct_output_set(PIN_UI_DIRECT_1, 0);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
        if (leds != previous_leds || (int)orange != previous_orange ||
            (int)blue != previous_blue || (int)timer_led != previous_timer) {
            ESP_LOGI(TAG, "L,%u,%u,%u,%u,%d,%d", leds, orange, blue, timer_led,
                     panel_err, timer_err);
            previous_leds = leds;
            previous_orange = orange;
            previous_blue = blue;
            previous_timer = timer_led;
        }
#else
        (void)panel_err;
        (void)timer_err;
#endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t indicators_init(void)
{
    return xTaskCreate(indicators_task, "indicators", 3072, NULL, 3, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}
