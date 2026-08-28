#include "ui_inputs.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pins.h"
#include "telemetry.h"

#define UI_MAIN_LONG_PRESS_MS 1500U

#ifndef MCL02M_ACTIVE_ZERO_DIAGNOSTICS
#define MCL02M_ACTIVE_ZERO_DIAGNOSTICS 0
#endif

static const char *TAG = "ui_inputs";
static i2c_master_bus_handle_t s_touch_bus;
static i2c_master_dev_handle_t s_touch_dev;
static QueueHandle_t s_encoder_queue;
static QueueHandle_t s_event_queue;

typedef enum {
    TOUCH_TRANSPORT_NONE,
    TOUCH_TRANSPORT_I2C,
    TOUCH_TRANSPORT_UART,
} touch_transport_t;

static touch_transport_t s_touch_transport;
static uint8_t s_touch_uart_last_raw = 0xff;

typedef enum {
    TOUCH_NONE,
    TOUCH_A,
    TOUCH_B,
    TOUCH_BOTH,
} touch_state_t;

static const char *touch_name(touch_state_t state)
{
    switch (state) {
    case TOUCH_A: return "TOUCH_A";
    case TOUCH_B: return "TOUCH_B";
    case TOUCH_BOTH: return "TOUCH_BOTH";
    default: return "NONE";
    }
}

static const char *touch_transport_name(void)
{
    switch (s_touch_transport) {
    case TOUCH_TRANSPORT_I2C: return "i2c";
    case TOUCH_TRANSPORT_UART: return "uart";
    default: return "none";
    }
}

static void post_event(ui_input_type_t type, int32_t value,
                       uint32_t duration_ms, int64_t timestamp_ms)
{
    if (s_event_queue == NULL) return;
    const ui_input_event_t event = {
        .type = type,
        .value = value,
        .duration_ms = duration_ms,
        .timestamp_ms = timestamp_ms,
    };
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"input_queue_full\"}",
                        timestamp_ms);
    }
}

static touch_state_t decode_touch(uint8_t raw)
{
    if (s_touch_transport == TOUCH_TRANSPORT_UART) {
        switch (raw) {
        case 0x08: return TOUCH_A;
        case 0x02: return TOUCH_B;
        case 0x0a: return TOUCH_BOTH;
        default: return TOUCH_NONE;
        }
    } else {
        switch (raw) {
        case 0x13: return TOUCH_A;
        case 0x12: return TOUCH_B;
        case 0x16: return TOUCH_BOTH;
        default: return TOUCH_NONE;
        }
    }
}

static esp_err_t touch_i2c_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_touch_bus);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x60,
        .scl_speed_hz = 10000,
    };
    err = i2c_master_bus_add_device(s_touch_bus, &dev_config, &s_touch_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_touch_bus);
        s_touch_bus = NULL;
    }
    return err;
}

static esp_err_t touch_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(UART_NUM_1, &config);
    if (err == ESP_OK) {
        /* Stock firmware leaves TX disconnected and receives touch bytes on GPIO19. */
        err = uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, PIN_TOUCH_SDA,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        uart_driver_delete(UART_NUM_1);
        return err;
    }
    uart_flush_input(UART_NUM_1);
    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    gpio_config_t strap_config = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_SCL,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&strap_config);
    if (err != ESP_OK) return err;

    /* The stock app samples GPIO18: LOW selects UART1, HIGH selects I2C1. */
    vTaskDelay(pdMS_TO_TICKS(2));
    const int strap = gpio_get_level(PIN_TOUCH_SCL);
    s_touch_transport = strap == 0 ? TOUCH_TRANSPORT_UART : TOUCH_TRANSPORT_I2C;
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"touch_transport\","
                    "\"gpio18_strap\":%d,\"selected\":\"%s\"}",
                    esp_timer_get_time() / 1000, strap, touch_transport_name());

    err = s_touch_transport == TOUCH_TRANSPORT_UART ? touch_uart_init() : touch_i2c_init();
    if (err != ESP_OK) s_touch_transport = TOUCH_TRANSPORT_NONE;
    return err;
}

static esp_err_t touch_read(uint8_t *raw)
{
    if (s_touch_transport == TOUCH_TRANSPORT_UART) {
        uint8_t encoded = 0;
        const int count = uart_read_bytes(UART_NUM_1, &encoded, 1, 0);
        if (count == 0) return ESP_ERR_NOT_FOUND;
        if (count < 0) return ESP_FAIL;

        const uint8_t low = encoded & 0x0f;
        const uint8_t high = (encoded >> 4) & 0x0f;
        if (high != ((~low) & 0x0f)) {
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"touch_uart_invalid\","
                            "\"encoded\":%u}", esp_timer_get_time() / 1000, encoded);
            return ESP_ERR_INVALID_CRC;
        }
        *raw = low;
        if (*raw != s_touch_uart_last_raw) {
            s_touch_uart_last_raw = *raw;
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"touch_uart_byte\","
                            "\"encoded\":%u,\"raw\":%u}",
                            esp_timer_get_time() / 1000, encoded, *raw);
        }
        return ESP_OK;
    }

    if (s_touch_transport == TOUCH_TRANSPORT_I2C && s_touch_bus != NULL && s_touch_dev != NULL) {
        /* Exact stock transaction: address-only write, 2 ticks, separate one-byte read. */
        esp_err_t err = i2c_master_probe(s_touch_bus, 0x60, 50);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(2));
        return i2c_master_receive(s_touch_dev, raw, 1, 5);
    }
    return ESP_ERR_INVALID_STATE;
}

static void encoder_edge_isr(void *arg)
{
    (void)arg;
    const uint8_t state = (gpio_get_level(PIN_ENCODER_PHASE_0) << 1) |
                          gpio_get_level(PIN_ENCODER_PHASE_1);
    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(s_encoder_queue, &state, &task_woken);
    if (task_woken == pdTRUE) portYIELD_FROM_ISR();
}

static void input_task(void *arg)
{
    (void)arg;
    static const int8_t quadrature_table[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0,
    };

    uint8_t encoder_previous = (gpio_get_level(PIN_ENCODER_PHASE_0) << 1) |
                               gpio_get_level(PIN_ENCODER_PHASE_1);
    int encoder_accumulator = 0;
    int encoder_position = 0;
    unsigned encoder_invalid_transitions = 0;

    int button_candidate = gpio_get_level(PIN_MAIN_BUTTON);
    int button_stable = button_candidate;
    unsigned button_candidate_count = 0;
    int64_t button_pressed_at_us = 0;
    bool button_long_posted = false;

    touch_state_t touch_candidate = TOUCH_NONE;
    touch_state_t touch_stable = TOUCH_NONE;
    unsigned touch_candidate_count = 0;
    uint8_t touch_raw = 0xff;

    int64_t next_touch_us = 0;
    unsigned touch_error_count = 0;

    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"inputs_ready\","
                    "\"button_gpio\":34,\"encoder_gpios\":[5,14],"
                    "\"touch_transport\":\"%s\",\"touch_addr\":\"0x60\"}",
                    esp_timer_get_time() / 1000, touch_transport_name());

    for (;;) {
        const int64_t now_us = esp_timer_get_time();

        const int button_raw = gpio_get_level(PIN_MAIN_BUTTON);
        if (button_raw != button_candidate) {
            button_candidate = button_raw;
            button_candidate_count = 1;
        } else if (button_candidate_count < 3) {
            button_candidate_count++;
        }
        if (button_candidate_count >= 3 && button_stable != button_candidate) {
            button_stable = button_candidate;
            if (button_stable == 0) {
                button_pressed_at_us = now_us;
                button_long_posted = false;
                post_event(UI_INPUT_MAIN_PRESSED, 1, 0, now_us / 1000);
                telemetry_emitf("{\"t_ms\":%lld,\"type\":\"main_button\","
                                "\"state\":\"pressed\",\"raw\":0}", now_us / 1000);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
                ESP_LOGI(TAG, "B,D");
#endif
            } else {
                const int64_t held_ms = button_pressed_at_us == 0 ? 0 :
                                        (now_us - button_pressed_at_us) / 1000;
                post_event(UI_INPUT_MAIN_RELEASED, 0, (uint32_t)held_ms,
                           now_us / 1000);
                telemetry_emitf("{\"t_ms\":%lld,\"type\":\"main_button\","
                                "\"state\":\"released\",\"raw\":1,"
                                "\"duration_ms\":%lld}", now_us / 1000, held_ms);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
                ESP_LOGI(TAG, "B,U,%lld", held_ms);
#endif
                button_pressed_at_us = 0;
            }
        }
        if (button_stable == 0 && button_pressed_at_us != 0 && !button_long_posted &&
            now_us - button_pressed_at_us >= UI_MAIN_LONG_PRESS_MS * 1000LL) {
            button_long_posted = true;
            post_event(UI_INPUT_MAIN_LONG, 1, UI_MAIN_LONG_PRESS_MS, now_us / 1000);
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"main_button\","
                            "\"state\":\"long\",\"duration_ms\":%u}",
                            now_us / 1000, UI_MAIN_LONG_PRESS_MS);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
            ESP_LOGI(TAG, "B,L,%u", UI_MAIN_LONG_PRESS_MS);
#endif
        }

        uint8_t encoder_now;
        while (xQueueReceive(s_encoder_queue, &encoder_now, 0) == pdTRUE) {
            if (encoder_now == encoder_previous) continue;
            const int8_t delta = quadrature_table[(encoder_previous << 2) | encoder_now];
            encoder_previous = encoder_now;
            if (delta == 0) {
                encoder_invalid_transitions++;
                continue;
            }
            encoder_accumulator += delta;
            /* This panel encoder produces two quadrature edges per physical detent. */
            if (encoder_accumulator >= 2 || encoder_accumulator <= -2) {
                const int detent = encoder_accumulator > 0 ? 1 : -1;
                encoder_accumulator = 0;
                encoder_position += detent;
                post_event(UI_INPUT_ENCODER, detent, 0, now_us / 1000);
                telemetry_emitf("{\"t_ms\":%lld,\"type\":\"encoder\","
                                "\"raw_0\":%u,\"raw_1\":%u,\"delta\":%d,"
                                "\"position\":%d,\"direction\":\"%s\","
                                "\"invalid_transition_count\":%u}", now_us / 1000,
                                (encoder_now >> 1) & 1, encoder_now & 1,
                                detent, encoder_position,
                                detent < 0 ? "right" : "left",
                                encoder_invalid_transitions);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
                ESP_LOGI(TAG, "E,%d,%d,%u", detent, encoder_position,
                         encoder_invalid_transitions);
#endif
            }
        }

        if (s_touch_transport != TOUCH_TRANSPORT_NONE && now_us >= next_touch_us) {
            const bool uart = s_touch_transport == TOUCH_TRANSPORT_UART;
            next_touch_us = now_us + (uart ? 2000 : (touch_error_count == 0 ? 20000 : 1000000));
            const esp_err_t err = touch_read(&touch_raw);
            if (err == ESP_OK) {
                touch_error_count = 0;
                const touch_state_t decoded = decode_touch(touch_raw);
                if (decoded != touch_candidate) {
                    touch_candidate = decoded;
                    touch_candidate_count = 1;
                } else if (touch_candidate_count < 2) {
                    touch_candidate_count++;
                }
                if (touch_candidate_count >= 2 && touch_stable != touch_candidate) {
                    const touch_state_t previous = touch_stable;
                    touch_stable = touch_candidate;
                    if (previous == TOUCH_A)
                        post_event(UI_INPUT_TOUCH_A_RELEASED, 0, 0, now_us / 1000);
                    else if (previous == TOUCH_B)
                        post_event(UI_INPUT_TOUCH_B_RELEASED, 0, 0, now_us / 1000);
                    else if (previous == TOUCH_BOTH)
                        post_event(UI_INPUT_TOUCH_BOTH_RELEASED, 0, 0, now_us / 1000);
                    if (touch_stable == TOUCH_A)
                        post_event(UI_INPUT_TOUCH_A_PRESSED, 1, 0, now_us / 1000);
                    else if (touch_stable == TOUCH_B)
                        post_event(UI_INPUT_TOUCH_B_PRESSED, 1, 0, now_us / 1000);
                    else if (touch_stable == TOUCH_BOTH)
                        post_event(UI_INPUT_TOUCH_BOTH_PRESSED, 1, 0, now_us / 1000);
                    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"touch\","
                                    "\"transport\":\"%s\",\"state\":\"%s\","
                                    "\"raw\":%u}", now_us / 1000,
                                    touch_transport_name(), touch_name(touch_stable), touch_raw);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
                    ESP_LOGI(TAG, "T,%s,%s,%02X", touch_transport_name(),
                             touch_name(touch_stable), touch_raw);
#endif
                }
            } else if (!(uart && err == ESP_ERR_NOT_FOUND)) {
                touch_error_count++;
                if (touch_error_count == 1 || (touch_error_count % 10) == 0) {
                    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"touch_error\","
                                    "\"transport\":\"%s\",\"esp_err\":%d,"
                                    "\"count\":%u}", now_us / 1000,
                                    touch_transport_name(), err, touch_error_count);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t ui_inputs_init(void)
{
    s_event_queue = xQueueCreate(32, sizeof(ui_input_event_t));
    if (s_event_queue == NULL) return ESP_ERR_NO_MEM;
    s_encoder_queue = xQueueCreate(64, sizeof(uint8_t));
    if (s_encoder_queue == NULL) return ESP_ERR_NO_MEM;

    gpio_config_t encoder_config = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_PHASE_0) |
                        (1ULL << PIN_ENCODER_PHASE_1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&encoder_config);
    if (err != ESP_OK) return err;

    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << PIN_MAIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&button_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = gpio_isr_handler_add(PIN_ENCODER_PHASE_0, encoder_edge_isr, NULL);
    if (err != ESP_OK) return err;
    err = gpio_isr_handler_add(PIN_ENCODER_PHASE_1, encoder_edge_isr, NULL);
    if (err != ESP_OK) return err;

    err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch I2C init failed: %s; GPIO inputs still work", esp_err_to_name(err));
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"touch_init_error\","
                        "\"esp_err\":%d}", esp_timer_get_time() / 1000, err);
    }

    if (xTaskCreate(input_task, "ui_inputs", 4096, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ui_inputs_get_event(ui_input_event_t *event, TickType_t wait_ticks)
{
    return event != NULL && s_event_queue != NULL &&
           xQueueReceive(s_event_queue, event, wait_ticks) == pdTRUE;
}
