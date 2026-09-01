#include "powerboard_control.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "pins.h"
#include "safety.h"
#include "telemetry.h"

#define PB_ADDRESS 0x2a
#define PB_CLOCK_HZ 10000
#define PB_READ_RESPONSE_DELAY_MS 30U

#define PB_REG_MASK(reg) ((uint16_t)(1U << ((reg) - 0x20U)))
#define PB_CRITICAL_READ_MASK \
    (PB_REG_MASK(0x20U) | PB_REG_MASK(0x22U) | PB_REG_MASK(0x23U) | \
     PB_REG_MASK(0x24U) | PB_REG_MASK(0x26U))
#define PB_SERVICE_READ_MASK \
    (PB_REG_MASK(0x21U) | PB_REG_MASK(0x25U) | PB_REG_MASK(0x27U))
#define PB_WRITE_0D_MASK (1U << 0)
#define PB_WRITE_00_MASK (1U << 1)
#define PB_WRITE_0C_MASK (1U << 2)
#define PB_ALL_WRITE_MASK \
    (PB_WRITE_0D_MASK | PB_WRITE_00_MASK | PB_WRITE_0C_MASK)

#define PB_RECOVERY_READ_SLOT_MS 40U
#define PB_RECOVERY_COMMAND_0D_MS 220U
#define PB_RECOVERY_COMMAND_00_MS 270U
#define PB_RECOVERY_COMMAND_0C_MS 274U

_Static_assert(PB_RECOVERY_COMMAND_0C_MS < MCL02M_I2C_RECOVERY_HEARTBEAT_MS,
               "recovery command schedule must fit its heartbeat");

#ifndef MCL02M_ACTIVE_ZERO_ENABLED
#define MCL02M_ACTIVE_ZERO_ENABLED 0
#endif

#ifndef MCL02M_ACTIVE_ZERO_DIAGNOSTICS
#define MCL02M_ACTIVE_ZERO_DIAGNOSTICS 0
#endif

#ifndef MCL02M_COMPACT_UART_TELEMETRY
#define MCL02M_COMPACT_UART_TELEMETRY 0
#endif

#define PB_ACTIVE_ZERO_0D 0x81U

#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
static const char *TAG = "pbdbg";
#endif

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_bus_lock;
static SemaphoreHandle_t s_status_lock;
static TaskHandle_t s_control_task;
static powerboard_status_t s_status;
static bool s_force_stop;
static int64_t s_arm_deadline_us;
static int64_t s_run_started_us;
static int64_t s_run_deadline_us;
static int64_t s_start_confirm_deadline_us;
static int64_t s_stop_started_us;
static int64_t s_stop_confirm_deadline_us;
static int64_t s_lease_deadline_us;
static int64_t s_transition_deadline_us;
static int64_t s_heartbeat_gap_deadline_us;
static unsigned s_no_pan_samples;
static unsigned s_r20_fault_samples;
static uint8_t s_r20_fault_value;
static bool s_unknown_r20_present;
static uint8_t s_unknown_r20_present_value;
static unsigned s_stop_active_samples;
static unsigned s_stop_zero_samples;
static unsigned s_igbt_raw_fault_samples;
static unsigned s_bottom_raw_fault_samples;
static unsigned s_igbt_limit_samples;
static unsigned s_bottom_limit_samples;
static uint32_t s_start_incident_sequence;
static uint32_t s_i2c_incident_sequence;
static int64_t s_i2c_critical_bad_since_us;
static int64_t s_i2c_command_bad_since_us;
static uint8_t s_last_successful_command_0d;
static uint8_t s_last_successful_command_0c;
static uint32_t s_transition_feedback_baseline;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
static bool s_feedback_reported;
static uint8_t s_reported_r20;
static uint8_t s_reported_r26;
#endif

/* Exact tables extracted from stock ESP32 firmware 2.2.0_0016. */
static const uint8_t s_bottom_ntc_lut_0b_fb[] = {
    0xf8,0xf2,0xeb,0xe5,0xe0,0xdc,0xd8,0xd5,0xd1,0xce,0xcb,0xc8,0xc5,0xc3,0xc1,0xbf,
    0xbd,0xbc,0xba,0xb9,0xb8,0xb7,0xb5,0xb4,0xb2,0xb1,0xaf,0xae,0xad,0xac,0xab,0xaa,
    0xa9,0xa8,0xa7,0xa6,0xa5,0xa4,0xa3,0xa2,0xa1,0xa0,0x9f,0x9e,0x9d,0x9c,0x9b,0x9a,
    0x99,0x98,0x97,0x96,0x95,0x94,0x93,0x93,0x92,0x91,0x90,0x90,0x8f,0x8e,0x8d,0x8c,
    0x8c,0x8b,0x8a,0x8a,0x89,0x89,0x88,0x88,0x87,0x86,0x85,0x85,0x84,0x83,0x83,0x82,
    0x81,0x81,0x80,0x80,0x7f,0x7e,0x7e,0x7d,0x7d,0x7c,0x7c,0x7b,0x7b,0x7a,0x7a,0x79,
    0x79,0x78,0x78,0x77,0x77,0x76,0x76,0x75,0x75,0x74,0x74,0x73,0x73,0x72,0x71,0x71,
    0x70,0x6f,0x6f,0x6e,0x6d,0x6d,0x6c,0x6c,0x6b,0x6b,0x6a,0x6a,0x69,0x69,0x68,0x68,
    0x67,0x67,0x66,0x66,0x65,0x65,0x64,0x64,0x63,0x63,0x62,0x62,0x61,0x61,0x60,0x60,
    0x5f,0x5f,0x5e,0x5e,0x5d,0x5d,0x5c,0x5c,0x5b,0x5b,0x5a,0x5a,0x59,0x58,0x58,0x57,
    0x56,0x56,0x55,0x55,0x54,0x53,0x53,0x52,0x51,0x51,0x50,0x50,0x4f,0x4e,0x4e,0x4d,
    0x4c,0x4c,0x4b,0x4a,0x49,0x49,0x48,0x48,0x47,0x46,0x46,0x45,0x45,0x44,0x44,0x43,
    0x43,0x42,0x42,0x41,0x41,0x40,0x40,0x3f,0x3f,0x3e,0x3e,0x3d,0x3c,0x3c,0x3b,0x3b,
    0x3a,0x39,0x38,0x37,0x36,0x35,0x34,0x33,0x32,0x31,0x30,0x2f,0x2e,0x2d,0x2c,0x2b,
    0x2a,0x29,0x27,0x25,0x23,0x21,0x1f,0x1c,0x19,0x16,0x12,0x0f,0x0c,0x09,0x06,0x03,
    0x00,
};

static const uint8_t s_igbt_ntc_lut_41_f7[] = {
    0x7d,0x7c,0x7b,0x7b,0x7a,0x79,0x78,0x78,0x77,0x76,0x76,0x75,0x74,0x74,0x73,0x72,
    0x72,0x71,0x70,0x70,0x6f,0x6e,0x6e,0x6d,0x6d,0x6c,0x6b,0x6b,0x6a,0x6a,0x69,0x68,
    0x68,0x67,0x67,0x66,0x66,0x65,0x64,0x64,0x63,0x63,0x62,0x62,0x61,0x61,0x60,0x60,
    0x5f,0x5f,0x5e,0x5d,0x5d,0x5c,0x5c,0x5b,0x5b,0x5a,0x5a,0x59,0x59,0x58,0x58,0x57,
    0x57,0x56,0x56,0x55,0x55,0x54,0x54,0x53,0x53,0x52,0x52,0x51,0x51,0x50,0x50,0x4f,
    0x4f,0x4e,0x4e,0x4e,0x4d,0x4d,0x4c,0x4c,0x4b,0x4b,0x4a,0x4a,0x49,0x49,0x48,0x48,
    0x47,0x47,0x46,0x46,0x45,0x45,0x44,0x44,0x43,0x43,0x42,0x41,0x41,0x40,0x40,0x3f,
    0x3f,0x3e,0x3e,0x3d,0x3d,0x3c,0x3c,0x3b,0x3b,0x3a,0x39,0x39,0x38,0x38,0x37,0x37,
    0x36,0x35,0x35,0x34,0x34,0x33,0x32,0x32,0x31,0x31,0x30,0x2f,0x2f,0x2e,0x2d,0x2d,
    0x2c,0x2b,0x2b,0x2a,0x29,0x28,0x28,0x27,0x26,0x25,0x24,0x24,0x23,0x22,0x21,0x20,
    0x1f,0x1e,0x1d,0x1c,0x1b,0x1a,0x19,0x18,0x17,0x16,0x15,0x14,0x12,0x11,0x10,0x0e,
    0x0c,0x0b,0x09,0x07,0x05,0x03,0x00,
};

_Static_assert(sizeof(s_bottom_ntc_lut_0b_fb) == 0xfb - 0x0b + 1, "bottom LUT");
_Static_assert(sizeof(s_igbt_ntc_lut_41_f7) == 0xf7 - 0x41 + 1, "IGBT LUT");

static uint32_t remaining_ms(int64_t deadline_us, int64_t now_us)
{
    if (deadline_us <= now_us) return 0;
    const int64_t delta = deadline_us - now_us;
    return (uint32_t)((delta + 999) / 1000);
}

static uint32_t elapsed_ms(int64_t started_us, int64_t now_us)
{
    if (started_us == 0 || now_us <= started_us) return 0;
    const uint64_t elapsed = (uint64_t)(now_us - started_us) / 1000U;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static uint8_t bottom_temperature(uint8_t raw)
{
    if (raw < 0x0b) return 0xfa;
    if (raw >= 0xfc) return 0;
    return s_bottom_ntc_lut_0b_fb[raw - 0x0b];
}

static uint8_t igbt_temperature(uint8_t raw)
{
    if (raw < 0x41) return 0x7d;
    if (raw >= 0xf8) return 0;
    return s_igbt_ntc_lut_41_f7[raw - 0x41];
}

static uint8_t topology_for_gear(uint8_t gear)
{
    if (gear == 0) return 0x00;
    if (gear <= MCL02M_LOW_TOPOLOGY_MAX_GEAR) return 0xa1;
    if (gear < MCL02M_HIGH_TOPOLOGY_MIN_GEAR) return 0xc1;
    return 0xe1;
}

static uint8_t cookware_limited_gear(uint8_t gear, bool limited)
{
    return limited && gear > MCL02M_SMALL_COOKWARE_MAX_GEAR ?
           MCL02M_SMALL_COOKWARE_MAX_GEAR : gear;
}

static bool r20_known_fault(uint8_t value)
{
    return value == 0x01 || value == 0x0b || value == 0x0c ||
           value == 0x15 || value == 0x16 || value == 0x17 ||
           value == 0x18 || value == 0x19 || value == 0x1a ||
           value == 0x1b || value == 0x1c || value == 0x1d;
}

static const char *r20_fault_name(uint8_t value)
{
    if (value == 0x0b) return "E03 HIGH VOLT";
    if (value == 0x0c) return "E04 LOW VOLT";
    if (value == 0x1b) return "E05 BOTTOM";
    if (value == 0x17) return "E07 IGBT";
    if (value == 0x15 || value == 0x16 || value == 0x18) return "E08 SENSOR";
    if (value == 0x19 || value == 0x1a || value == 0x1c || value == 0x1d)
        return "E10 CHANNEL";
    return "E12 POWER";
}

static bool r20_silent_nonfault(uint8_t value)
{
    return value == 0 || value == 0x02 || value == 0x2b ||
           value == 0x29 || value == 0x2a;
}

static bool r20_session_compatible(uint8_t value)
{
    return value != 0x02 && !r20_known_fault(value);
}

static bool r20_proves_pan_present(uint8_t value)
{
    return value == 0 || value == 0x2b || value == 0x29 || value == 0x2a;
}

static bool state_can_energize(powerboard_state_t state)
{
    return state == PB_STATE_STARTING || state == PB_STATE_HEATING ||
           state == PB_STATE_NO_PAN || state == PB_STATE_HEARTBEAT_GAP;
}

static bool state_session_open(powerboard_state_t state)
{
    return state_can_energize(state) || state == PB_STATE_ACTIVE_ZERO ||
           state == PB_STATE_PAUSED;
}

static bool bottom_temperature_interface_safe(uint8_t temperature_c)
{
#if MCL02M_MAX_BOTTOM_C == 0U
    (void)temperature_c;
    return true;
#else
    return temperature_c <= MCL02M_MAX_BOTTOM_C;
#endif
}

const char *powerboard_state_name(powerboard_state_t state)
{
    switch (state) {
    case PB_STATE_BOOT: return "BOOT";
    case PB_STATE_STOPPED: return "STOPPED";
    case PB_STATE_ARMED: return "ARMED";
    case PB_STATE_STARTING: return "STARTING";
    case PB_STATE_HEATING: return "HEATING";
    case PB_STATE_ACTIVE_ZERO: return "ACTIVE_ZERO";
    case PB_STATE_PAUSED: return "PAUSED";
    case PB_STATE_NO_PAN: return "NO_PAN";
    case PB_STATE_HEARTBEAT_GAP: return "HB_GAP";
    case PB_STATE_STOPPING: return "STOPPING";
    case PB_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

const char *powerboard_transition_name(powerboard_transition_t transition)
{
    switch (transition) {
    case PB_TRANSITION_NONE: return "NONE";
    case PB_TRANSITION_START: return "START";
    case PB_TRANSITION_ACTIVE_ZERO: return "ACTIVE_ZERO";
    case PB_TRANSITION_PAUSE: return "PAUSE";
    case PB_TRANSITION_RESUME: return "RESUME";
    case PB_TRANSITION_PAN_RETURN_HOLD: return "PAN_RETURN_HOLD";
    case PB_TRANSITION_PAN_RETURN_RESUME: return "PAN_RETURN_RESUME";
    default: return "UNKNOWN";
    }
}

const char *powerboard_feedback_state_name(powerboard_feedback_state_t state)
{
    switch (state) {
    case PB_FEEDBACK_UNKNOWN: return "UNKNOWN";
    case PB_FEEDBACK_OUTPUT_OFF: return "OUTPUT_OFF";
    case PB_FEEDBACK_SESSION_ACTIVE: return "SESSION_ACTIVE";
    case PB_FEEDBACK_NO_PAN: return "NO_PAN";
    case PB_FEEDBACK_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

static esp_err_t bus_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_POWERBOARD_SDA,
        .scl_io_num = PIN_POWERBOARD_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) return err;

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PB_ADDRESS,
        .scl_speed_hz = PB_CLOCK_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    return err;
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    if (value == NULL || !mcl02m_powerboard_read_selector_allowed(reg)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t response[2] = {0};
    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit(s_device, &reg, 1, 50);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(PB_READ_RESPONSE_DELAY_MS));
        err = i2c_master_receive(s_device, response, sizeof(response), 50);
    }
    xSemaphoreGive(s_bus_lock);
    if (err != ESP_OK) return err;
    if (response[1] != (uint8_t)(reg + response[0])) return ESP_ERR_INVALID_CRC;
    *value = response[0];
    return ESP_OK;
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    if (!mcl02m_powerboard_control_register_allowed(reg)) return ESP_ERR_INVALID_ARG;
    const uint8_t frame[3] = {reg, value, (uint8_t)(reg + value)};
    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    const esp_err_t err = i2c_master_transmit(s_device, frame, sizeof(frame), 50);
    xSemaphoreGive(s_bus_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"pb_write\",\"reg\":%u,"
                    "\"value\":%u,\"checksum\":%u,\"err\":%d}",
                    esp_timer_get_time() / 1000, reg, value, frame[2], err);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    if (err != ESP_OK) ESP_LOGW(TAG, "I,W,%02X,%02X,%d", reg, value, err);
#endif
    return err;
}

static esp_err_t send_stop_sequence(uint8_t *attempt_mask, uint8_t *error_mask)
{
    uint8_t attempted = 0;
    uint8_t failed = 0;
    attempted |= PB_WRITE_0D_MASK;
    esp_err_t result = write_register(0x0d, 0x00);
    if (result != ESP_OK) failed |= PB_WRITE_0D_MASK;
    vTaskDelay(pdMS_TO_TICKS(50));
    attempted |= PB_WRITE_00_MASK;
    const esp_err_t e00 = write_register(0x00, 0x00);
    if (e00 != ESP_OK) failed |= PB_WRITE_00_MASK;
    vTaskDelay(pdMS_TO_TICKS(4));
    attempted |= PB_WRITE_0C_MASK;
    const esp_err_t e0c = write_register(0x0c, 0x00);
    if (e0c != ESP_OK) failed |= PB_WRITE_0C_MASK;
    if (result == ESP_OK && e00 != ESP_OK) result = e00;
    if (result == ESP_OK && e0c != ESP_OK) result = e0c;
    if (attempt_mask != NULL) *attempt_mask |= attempted;
    if (error_mask != NULL) *error_mask |= failed;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.last_command_0d = 0;
    s_status.last_command_00 = 0;
    s_status.last_command_0c = 0;
    xSemaphoreGive(s_status_lock);
    return result;
}

static void store_register(uint8_t reg, uint8_t value, esp_err_t err)
{
    const unsigned bit = reg - 0x20U;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (err == ESP_OK) {
        s_status.registers[bit] = value;
        s_status.valid_mask |= (uint16_t)(1U << bit);
        if (reg == 0x23) s_status.igbt_c = igbt_temperature(value);
        if (reg == 0x24) s_status.bottom_c = bottom_temperature(value);
    } else {
        s_status.valid_mask &= (uint16_t)~(1U << bit);
    }
    xSemaphoreGive(s_status_lock);
}

static esp_err_t read_and_store(uint8_t reg)
{
    uint8_t value = 0;
    const esp_err_t err = read_register(reg, &value);
    store_register(reg, value, err);
    if (err != ESP_OK) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"pb_read_error\","
                        "\"reg\":%u,\"err\":%d}",
                        esp_timer_get_time() / 1000, reg, err);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
        ESP_LOGW(TAG, "I,R,%02X,%d", reg, err);
#endif
    }
    return err;
}

static void capture_start_incident_locked(const char *reason,
                                          powerboard_state_t previous_state)
{
    if (reason == NULL || strcmp(reason, "START TIMEOUT") != 0 ||
        s_status.start_incident.valid) {
        return;
    }

    powerboard_start_incident_t *incident = &s_status.start_incident;
    incident->valid = true;
    incident->sequence = ++s_start_incident_sequence;
    incident->timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000);
    incident->lower_state = previous_state;
    incident->r20 = s_status.registers[0];
    incident->r26 = s_status.registers[6];
    incident->r28 = s_status.registers[8];
    incident->valid_mask = s_status.valid_mask;
    incident->requested_gear = s_status.target_gear;
    incident->transmitted_gear = s_last_successful_command_0c;
    incident->transmitted_topology = s_last_successful_command_0d;
    incident->command_0d = s_status.last_command_0d;
    incident->command_00 = s_status.last_command_00;
    incident->command_0c = s_status.last_command_0c;
    incident->completed_cycles = s_status.completed_cycles;
    incident->bad_cycles = s_status.bad_cycles;
    incident->consecutive_bad_cycles = s_status.consecutive_bad_cycles;
    strlcpy(incident->reason, reason, sizeof(incident->reason));

#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGW(TAG,
             "X,%" PRIu32 ",%" PRIu64 ",%s,%02X,%02X,%02X,%04X,"
             "%u,%u,%02X,%02X,%02X,%02X,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%s",
             incident->sequence, incident->timestamp_ms,
             powerboard_state_name(incident->lower_state), incident->r20,
             incident->r26, incident->r28, incident->valid_mask,
             incident->requested_gear, incident->transmitted_gear,
             incident->transmitted_topology, incident->command_0d,
             incident->command_00, incident->command_0c,
             incident->completed_cycles, incident->bad_cycles,
             incident->consecutive_bad_cycles, incident->reason);
#endif
}

static void capture_i2c_incident_locked(const char *reason,
                                        powerboard_state_t previous_state,
                                        uint16_t read_error_mask,
                                        uint8_t write_error_mask,
                                        uint32_t critical_loss_ms,
                                        uint32_t command_loss_ms)
{
    if (s_status.i2c_incident.valid) return;

    powerboard_i2c_incident_t *incident = &s_status.i2c_incident;
    incident->valid = true;
    incident->sequence = ++s_i2c_incident_sequence;
    incident->timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000);
    incident->lower_state = previous_state;
    incident->read_error_mask = read_error_mask;
    incident->write_error_mask = write_error_mask;
    incident->valid_mask = s_status.valid_mask;
    incident->r20 = s_status.registers[0];
    incident->r26 = s_status.registers[6];
    incident->command_0d = s_status.last_command_0d;
    incident->command_00 = s_status.last_command_00;
    incident->command_0c = s_status.last_command_0c;
    incident->completed_cycles = s_status.completed_cycles;
    incident->bad_cycles = s_status.bad_cycles;
    incident->consecutive_bad_cycles = s_status.consecutive_bad_cycles;
    incident->critical_loss_ms = critical_loss_ms;
    incident->command_loss_ms = command_loss_ms;
    incident->recovery_entries = s_status.i2c_recovery_entries;
    strlcpy(incident->reason, reason == NULL ? "I2C LOST" : reason,
            sizeof(incident->reason));

#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGE(TAG,
             "I,F,%" PRIu32 ",%" PRIu64 ",%s,%04X,%02X,%04X,%02X,%02X,"
             "%02X,%02X,%02X,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%s",
             incident->sequence, incident->timestamp_ms,
             powerboard_state_name(incident->lower_state),
             incident->read_error_mask, incident->write_error_mask,
             incident->valid_mask, incident->r20, incident->r26,
             incident->command_0d, incident->command_00, incident->command_0c,
             incident->critical_loss_ms, incident->command_loss_ms,
             incident->recovery_entries, incident->reason);
#endif
}

static void reset_i2c_health_locked(bool clear_incident)
{
    s_i2c_critical_bad_since_us = 0;
    s_i2c_command_bad_since_us = 0;
    s_status.consecutive_bad_cycles = 0;
    s_status.i2c_recovery_active = false;
    s_status.i2c_recovery_good_cycles = 0;
    s_status.i2c_critical_loss_ms = 0;
    s_status.i2c_command_loss_ms = 0;
    s_status.last_i2c_read_error_mask = 0;
    s_status.last_i2c_write_error_mask = 0;
    if (clear_incident)
        memset(&s_status.i2c_incident, 0, sizeof(s_status.i2c_incident));
}

static void start_stop_evidence_locked(const char *reason)
{
    ++s_status.stop_generation;
    s_status.stop_confirm_samples = 0;
    s_status.stop_verified = false;
    s_status.stop_timed_out = false;
    s_status.stop_elapsed_ms = 0;
    s_stop_zero_samples = 0;
    s_stop_started_us = esp_timer_get_time();
    s_stop_confirm_deadline_us = s_stop_started_us +
        (int64_t)MCL02M_STOP_CONFIRM_TIMEOUT_MS * 1000;
    strlcpy(s_status.stop_reason, reason == NULL ? "STOP" : reason,
            sizeof(s_status.stop_reason));
    strlcpy(s_status.stop_issue, "NONE", sizeof(s_status.stop_issue));
}

static void freeze_stop_evidence_locked(void)
{
    if (s_stop_started_us != 0) {
        const int64_t now_us = esp_timer_get_time();
        s_status.stop_elapsed_ms =
            (uint32_t)((now_us - s_stop_started_us) / 1000);
        s_stop_started_us = 0;
    }
    s_stop_confirm_deadline_us = 0;
}

static void record_stop_issue_locked(const char *issue)
{
    if (issue == NULL || strcmp(s_status.stop_issue, "NONE") != 0) return;
    strlcpy(s_status.stop_issue, issue, sizeof(s_status.stop_issue));
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGW(TAG, "S,WAIT,%" PRIu32 ",%s", s_status.stop_generation,
             s_status.stop_issue);
#endif
}

static void invalidate_lease_locked(void)
{
    s_status.lease_active = false;
    s_lease_deadline_us = 0;
}

static void cancel_transition_locked(const char *reason)
{
    if (!s_status.transition_pending) return;
    s_status.transition_pending = false;
    s_status.transition_command_transmitted = false;
    s_transition_deadline_us = 0;
    s_start_confirm_deadline_us = 0;
    strlcpy(s_status.transition_result, reason == NULL ? "CANCELLED" : reason,
            sizeof(s_status.transition_result));
}

static void reject_transition_locked(const char *reason)
{
    ++s_status.transition_rejection_sequence;
    if (s_status.transition_rejection_sequence == 0)
        ++s_status.transition_rejection_sequence;
    strlcpy(s_status.transition_rejection,
            reason == NULL ? "REJECTED" : reason,
            sizeof(s_status.transition_rejection));
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGW(TAG, "T,REJECT,%" PRIu32 ",%s",
             s_status.transition_rejection_sequence,
             s_status.transition_rejection);
#endif
}

static void begin_transition_locked(powerboard_transition_t kind,
                                    powerboard_state_t requested_state,
                                    uint8_t requested_gear)
{
    ++s_status.transition_generation;
    if (s_status.transition_generation == 0) ++s_status.transition_generation;
    s_status.transition_kind = kind;
    s_status.transition_requested_state = requested_state;
    s_status.transition_requested_gear = requested_gear;
    s_status.transition_pending = true;
    s_status.transition_command_transmitted = false;
    s_status.confirmation_inferred = false;
    s_transition_feedback_baseline = s_status.feedback_sequence;
    s_transition_deadline_us = 0;
    s_start_confirm_deadline_us = 0;
    strlcpy(s_status.transition_result, "PENDING",
            sizeof(s_status.transition_result));
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG, "T,BEGIN,%" PRIu32 ",%s,%s,%u",
             s_status.transition_generation,
             powerboard_transition_name(kind),
             powerboard_state_name(requested_state), requested_gear);
#endif
}

static void finish_transition_locked(void)
{
    const powerboard_transition_t kind = s_status.transition_kind;
    const powerboard_state_t requested_state = s_status.transition_requested_state;
    const uint8_t requested_gear = s_status.transition_requested_gear;
    const uint8_t confirmed_gear = cookware_limited_gear(
        requested_gear, s_status.cookware_limited);
    s_status.state = requested_state;
    s_status.applied_gear = confirmed_gear;
    if (confirmed_gear != 0) s_status.topology = topology_for_gear(confirmed_gear);
    s_status.transition_pending = false;
    s_status.transition_command_transmitted = false;
    s_status.transition_confirmed_generation = s_status.transition_generation;
    s_status.confirmed_state = requested_state;
    s_status.confirmed_gear = confirmed_gear;
    s_status.confirmation_inferred = true;
    s_transition_deadline_us = 0;
    s_start_confirm_deadline_us = 0;
    strlcpy(s_status.transition_result, "CONFIRMED",
            sizeof(s_status.transition_result));
    if (kind == PB_TRANSITION_ACTIVE_ZERO || kind == PB_TRANSITION_PAUSE ||
        kind == PB_TRANSITION_PAN_RETURN_HOLD ||
        (kind == PB_TRANSITION_START && requested_state == PB_STATE_ACTIVE_ZERO))
        ++s_status.active_zero_entries;
    if ((kind == PB_TRANSITION_RESUME ||
         kind == PB_TRANSITION_PAN_RETURN_RESUME) && confirmed_gear != 0)
        ++s_status.active_zero_resumes;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG, "T,DONE,%" PRIu32 ",%s,%s,%u,%02X,%02X",
             s_status.transition_generation,
             powerboard_transition_name(kind),
             powerboard_state_name(requested_state), confirmed_gear,
             s_status.feedback_r20, s_status.feedback_r26);
#endif
}

static void enter_no_pan_locked(const char *reason)
{
    cancel_transition_locked(reason == NULL ? "NO PAN" : reason);
    s_status.state = PB_STATE_NO_PAN;
    s_status.applied_gear = 0;
    s_status.confirmed_state = PB_STATE_NO_PAN;
    s_status.confirmed_gear = 0;
    s_status.confirmation_inferred = false;
    s_no_pan_samples = MCL02M_NO_PAN_SAMPLES;
}

static void fault_locked(const char *reason)
{
    if (s_status.state == PB_STATE_FAULT) {
        /* Preserve the first cause; later safety observations must not hide it. */
        s_force_stop = true;
        return;
    }
    const powerboard_state_t previous_state = s_status.state;
    const char *fault_reason = reason == NULL ? "FAULT" : reason;
    capture_start_incident_locked(fault_reason, previous_state);
    cancel_transition_locked(fault_reason);
    start_stop_evidence_locked(fault_reason);
    invalidate_lease_locked();
    s_status.state = PB_STATE_FAULT;
    s_status.target_gear = 0;
    s_status.applied_gear = 0;
    s_status.topology = 0;
    s_status.cookware_limited = false;
    strlcpy(s_status.fault, fault_reason, sizeof(s_status.fault));
    s_run_deadline_us = 0;
    s_arm_deadline_us = 0;
    s_start_confirm_deadline_us = 0;
    s_heartbeat_gap_deadline_us = 0;
    s_force_stop = true;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGW(TAG, "F,%s,%02X,%02X,%02X,%02X,%02X,%s",
             powerboard_state_name(previous_state), s_status.registers[0],
             s_status.registers[6], s_status.last_command_0d,
             s_status.last_command_00, s_status.last_command_0c, fault_reason);
#endif
}

static void update_i2c_health_locked(int64_t now_us,
                                     uint16_t read_attempt_mask,
                                     uint16_t read_error_mask,
                                     bool command_expected,
                                     uint8_t write_attempt_mask,
                                     uint8_t write_error_mask)
{
    const bool critical_read_bad =
        (read_error_mask & PB_CRITICAL_READ_MASK) != 0;
    const bool service_read_bad =
        (read_error_mask & PB_SERVICE_READ_MASK) != 0;
    const bool command_bad = write_error_mask != 0;
    const bool critical_bad = critical_read_bad || command_bad;
    const bool critical_reads_complete =
        (read_attempt_mask & PB_CRITICAL_READ_MASK) == PB_CRITICAL_READ_MASK;
    const bool commands_complete = !command_expected ||
        (write_attempt_mask & PB_ALL_WRITE_MASK) == PB_ALL_WRITE_MASK;
    const bool complete_good_cycle = critical_reads_complete && commands_complete &&
        !critical_bad;

    s_status.last_i2c_read_error_mask = read_error_mask;
    s_status.last_i2c_write_error_mask = write_error_mask;
    ++s_status.completed_cycles;
    if (read_error_mask != 0 || write_error_mask != 0) ++s_status.bad_cycles;
    if (critical_bad) ++s_status.critical_bad_cycles;
    if (service_read_bad) ++s_status.service_bad_cycles;

    if (critical_bad) {
        ++s_status.consecutive_bad_cycles;
        s_status.i2c_recovery_good_cycles = 0;
        if (s_i2c_critical_bad_since_us == 0)
            s_i2c_critical_bad_since_us = now_us;
        if (!s_status.i2c_recovery_active &&
            s_status.consecutive_bad_cycles >=
                MCL02M_I2C_RECOVERY_TRIGGER_CYCLES &&
            s_status.state != PB_STATE_STOPPED &&
            s_status.state != PB_STATE_FAULT) {
            s_status.i2c_recovery_active = true;
            ++s_status.i2c_recovery_entries;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
            ESP_LOGW(TAG, "I,REC,1,%" PRIu32 ",%04X,%02X",
                     s_status.consecutive_bad_cycles, read_error_mask,
                     write_error_mask);
#endif
        }
    } else if (complete_good_cycle) {
        s_status.consecutive_bad_cycles = 0;
        s_i2c_critical_bad_since_us = 0;
        if (s_status.i2c_recovery_active) {
            if (s_status.i2c_recovery_good_cycles < UINT8_MAX)
                ++s_status.i2c_recovery_good_cycles;
            if (s_status.i2c_recovery_good_cycles >=
                MCL02M_I2C_RECOVERY_GOOD_CYCLES) {
                s_status.i2c_recovery_active = false;
                s_status.i2c_recovery_good_cycles = 0;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
                ESP_LOGI(TAG, "I,REC,0,GOOD");
#endif
            }
        }
    }

    if (command_bad) {
        if (s_i2c_command_bad_since_us == 0)
            s_i2c_command_bad_since_us = now_us;
    } else if (commands_complete) {
        s_i2c_command_bad_since_us = 0;
    }

    s_status.i2c_critical_loss_ms =
        elapsed_ms(s_i2c_critical_bad_since_us, now_us);
    s_status.i2c_command_loss_ms =
        elapsed_ms(s_i2c_command_bad_since_us, now_us);

    const bool command_timed_out = s_status.i2c_command_loss_ms >=
        MCL02M_I2C_COMMAND_LOSS_TIMEOUT_MS;
    const bool critical_timed_out = s_status.i2c_critical_loss_ms >=
        MCL02M_I2C_CRITICAL_LOSS_TIMEOUT_MS;
    if ((command_timed_out || critical_timed_out) &&
        s_status.state != PB_STATE_STOPPED &&
        s_status.state != PB_STATE_FAULT) {
        const char *incident_reason = command_timed_out ?
            "COMMAND LOSS" : "CRITICAL LOSS";
        capture_i2c_incident_locked(incident_reason, s_status.state,
                                    read_error_mask, write_error_mask,
                                    s_status.i2c_critical_loss_ms,
                                    s_status.i2c_command_loss_ms);
        if (s_status.state == PB_STATE_STOPPING) {
            record_stop_issue_locked("I2C LOST");
            s_force_stop = true;
        } else {
            fault_locked("I2C LOST");
        }
    }
}

static void begin_stop_locked(const char *reason)
{
    if (s_status.state == PB_STATE_STOPPING || s_status.state == PB_STATE_FAULT) {
        /* Repeated Stop is idempotent and keeps the first origin/deadline. */
        s_force_stop = true;
        return;
    }
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    const powerboard_state_t previous_state = s_status.state;
#endif
    cancel_transition_locked(reason == NULL ? "STOP" : reason);
    start_stop_evidence_locked(reason);
    invalidate_lease_locked();
    s_status.state = PB_STATE_STOPPING;
    s_status.target_gear = 0;
    s_status.applied_gear = 0;
    s_status.topology = 0;
    s_status.cookware_limited = false;
    s_arm_deadline_us = 0;
    s_run_started_us = 0;
    s_run_deadline_us = 0;
    s_start_confirm_deadline_us = 0;
    s_heartbeat_gap_deadline_us = 0;
    s_no_pan_samples = 0;
    s_r20_fault_samples = 0;
    s_force_stop = true;
    strlcpy(s_status.fault, reason == NULL ? "STOP" : reason, sizeof(s_status.fault));
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG, "S,BEGIN,%" PRIu32 ",%s,%s", s_status.stop_generation,
             powerboard_state_name(previous_state), s_status.stop_reason);
#endif
}

static void expire_lease_locked(void)
{
    if (!s_status.lease_active) return;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    const uint32_t generation = s_status.lease_generation;
#endif
    s_status.lease_expired = true;
    ++s_status.lease_expirations;
    begin_stop_locked("COOK LEASE");
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGE(TAG, "L,EXPIRE,%" PRIu32 ",%" PRIu32,
             generation, s_status.lease_expirations);
#endif
}

static void finish_stop_locked(void)
{
    s_status.state = PB_STATE_STOPPED;
    s_status.target_gear = 0;
    s_status.applied_gear = 0;
    s_status.topology = 0;
    s_status.stop_verified = true;
    s_status.confirmed_state = PB_STATE_STOPPED;
    s_status.confirmed_gear = 0;
    s_status.confirmation_inferred = false;
    s_status.stop_confirm_samples = MCL02M_STOP_CONFIRM_SAMPLES;
    freeze_stop_evidence_locked();
    s_stop_zero_samples = MCL02M_STOP_CONFIRM_SAMPLES;
    s_force_stop = false;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG, "S,DONE,%" PRIu32 ",%s", s_status.stop_generation,
             s_status.stop_reason);
#endif
}

static const char *preflight_issue_locked(void)
{
    const uint16_t needed = (1U << 0) | (1U << 2) | (1U << 3) |
                            (1U << 4) | (1U << 6);
    if ((s_status.valid_mask & needed) != needed) return "READINGS INVALID";
    if (s_status.registers[0] != 0) return "R20 NONZERO";
    if (s_status.registers[6] != 0) return "R26 NONZERO";
    if (s_status.registers[3] < 0x41 || s_status.registers[3] >= 0xf8)
        return "IGBT SENSOR";
    if (s_status.registers[4] < 0x0b || s_status.registers[4] >= 0xfc)
        return "BOTTOM SENSOR";
#if MCL02M_IGBT_START_INHIBIT_C > 0U
    if (s_status.igbt_c > MCL02M_IGBT_START_INHIBIT_C)
        return "IGBT START HOT";
#endif
#if MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED
    if (s_status.igbt_c > MCL02M_MAX_IGBT_C) return "IGBT LIMIT";
#endif
    if (!bottom_temperature_interface_safe(s_status.bottom_c))
        return "BOTTOM LIMIT";
    return NULL;
}

static bool preflight_healthy_locked(void)
{
    return preflight_issue_locked() == NULL;
}

static const char *retained_session_issue_locked(void)
{
    const uint16_t needed = (1U << 0) | (1U << 2) | (1U << 3) |
                            (1U << 4) | (1U << 6);
    if ((s_status.valid_mask & needed) != needed) return "READINGS INVALID";
    if (!r20_session_compatible(s_status.registers[0])) return "R20 INCOMPATIBLE";
    if (s_status.registers[6] == 0) return "R26 OUTPUT OFF";
    if (s_status.registers[3] < 0x41 || s_status.registers[3] >= 0xf8)
        return "IGBT SENSOR";
    if (s_status.registers[4] < 0x0b || s_status.registers[4] >= 0xfc)
        return "BOTTOM SENSOR";
#if MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED
    if (s_status.igbt_c > MCL02M_MAX_IGBT_C) return "IGBT LIMIT";
#endif
    if (!bottom_temperature_interface_safe(s_status.bottom_c))
        return "BOTTOM LIMIT";
    return NULL;
}

static esp_err_t startup_probe(void)
{
    static const uint8_t required[] = {0x25, 0x28, 0x29, 0x24, 0x2a, 0x2b};
    static const uint8_t service[] = {0x2c, 0x2d, 0x2e, 0x2f};
    esp_err_t required_result = ESP_OK;
    uint16_t required_valid_mask = 0;
    uint16_t service_valid_mask = 0;
    uint8_t service_failures = 0;

    for (size_t i = 0; i < sizeof(required); ++i) {
        esp_err_t err = ESP_FAIL;
        for (unsigned attempt = 0; attempt < 5; ++attempt) {
            err = read_and_store(required[i]);
            if (err == ESP_OK) break;
        }
        esp_task_wdt_reset();
        if (err == ESP_OK) {
            required_valid_mask |= (uint16_t)(1U << (required[i] - 0x20U));
        } else if (required_result == ESP_OK) {
            required_result = err;
        }
    }
    for (size_t i = 0; i < sizeof(service); ++i) {
        esp_err_t err = ESP_FAIL;
        for (unsigned attempt = 0; attempt < 5; ++attempt) {
            err = read_and_store(service[i]);
            if (err == ESP_OK) break;
        }
        esp_task_wdt_reset();
        if (err == ESP_OK) {
            service_valid_mask |= (uint16_t)(1U << (service[i] - 0x20U));
        } else {
            ++service_failures;
        }
    }

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.startup_required_valid_mask = required_valid_mask;
    s_status.startup_service_valid_mask = service_valid_mask;
    s_status.startup_service_failures = service_failures;
    s_status.startup_required_ok = required_result == ESP_OK;
    xSemaphoreGive(s_status_lock);

    powerboard_status_t status;
    powerboard_control_get_status(&status);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"pb_startup\",\"r25\":%u,"
                    "\"r28\":%u,\"r29\":%u,\"r2a\":%u,\"r2b\":%u,"
                    "\"r2c\":%u,\"r2d\":%u,\"r2e\":%u,\"r2f\":%u,"
                    "\"required_mask\":%u,\"service_mask\":%u,"
                    "\"service_failures\":%u,\"err\":%d}",
                    esp_timer_get_time() / 1000,
                    status.registers[5], status.registers[8], status.registers[9],
                    status.registers[10], status.registers[11], status.registers[12],
                    status.registers[13], status.registers[14], status.registers[15],
                    required_valid_mask, service_valid_mask, service_failures,
                    required_result);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG,
             "B,%04X,%04X,%u,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X",
             required_valid_mask, service_valid_mask, service_failures,
             status.registers[5], status.registers[8], status.registers[9],
             status.registers[4], status.registers[10], status.registers[11],
             status.registers[12], status.registers[13], status.registers[14],
             status.registers[15]);
#endif
    return required_result;
}

static bool wait_until_or_stop(TickType_t cycle_start, unsigned offset_ms)
{
    const TickType_t target = cycle_start + pdMS_TO_TICKS(offset_ms);
    for (;;) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        const bool force_stop = s_force_stop;
        xSemaphoreGive(s_status_lock);
        if (force_stop) return false;
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(target - now) <= 0) return true;
        ulTaskNotifyTake(pdTRUE, target - now);
    }
}

static void update_time_and_safety(int64_t now_us)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_arm_deadline_us != 0 && now_us >= s_arm_deadline_us && s_status.state == PB_STATE_ARMED) {
        s_status.state = PB_STATE_STOPPED;
        s_arm_deadline_us = 0;
        strlcpy(s_status.fault, "ARM EXPIRED", sizeof(s_status.fault));
    }
    if (s_status.lease_active && s_lease_deadline_us != 0 &&
        now_us >= s_lease_deadline_us) {
        if (state_session_open(s_status.state)) {
            expire_lease_locked();
        } else {
            invalidate_lease_locked();
        }
    }
    if (s_run_deadline_us != 0 && now_us >= s_run_deadline_us &&
        state_session_open(s_status.state)) {
        begin_stop_locked("RUN LIMIT");
    }
    if (s_status.transition_pending && s_status.transition_command_transmitted &&
        s_transition_deadline_us != 0 && now_us >= s_transition_deadline_us) {
        const char *reason = "TRANSITION TIMEOUT";
        switch (s_status.transition_kind) {
        case PB_TRANSITION_START: reason = "START TIMEOUT"; break;
        case PB_TRANSITION_ACTIVE_ZERO: reason = "ZERO ACK TIMEOUT"; break;
        case PB_TRANSITION_PAUSE: reason = "PAUSE ACK TIMEOUT"; break;
        case PB_TRANSITION_RESUME: reason = "RESUME TIMEOUT"; break;
        case PB_TRANSITION_PAN_RETURN_HOLD: reason = "PAN HOLD TIMEOUT"; break;
        case PB_TRANSITION_PAN_RETURN_RESUME: reason = "PAN RESUME TIMEOUT"; break;
        default: break;
        }
        fault_locked(reason);
    }
    if (s_status.state == PB_STATE_HEARTBEAT_GAP && now_us >= s_heartbeat_gap_deadline_us) {
        fault_locked("HB GAP END");
    }
    if ((s_status.state == PB_STATE_STOPPING || s_status.state == PB_STATE_FAULT) &&
        s_stop_confirm_deadline_us != 0 &&
        now_us >= s_stop_confirm_deadline_us) {
        s_status.stop_timed_out = true;
        s_stop_confirm_deadline_us = 0;
        record_stop_issue_locked("STOP TIMEOUT");
        s_force_stop = true;
    }

    if (state_session_open(s_status.state)) {
        const bool igbt_valid = (s_status.valid_mask & (1U << 3)) != 0;
        const bool bottom_valid = (s_status.valid_mask & (1U << 4)) != 0;
        const bool igbt_raw_invalid = igbt_valid &&
            (s_status.registers[3] < 0x41 || s_status.registers[3] >= 0xf8);
        const bool bottom_raw_invalid = bottom_valid &&
            (s_status.registers[4] < 0x0b || s_status.registers[4] >= 0xfc);

        s_igbt_raw_fault_samples = igbt_raw_invalid ?
            s_igbt_raw_fault_samples + 1U : 0U;
        s_bottom_raw_fault_samples = bottom_raw_invalid ?
            s_bottom_raw_fault_samples + 1U : 0U;

#if MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED
        s_igbt_limit_samples = igbt_valid && !igbt_raw_invalid &&
            s_status.igbt_c > MCL02M_MAX_IGBT_C ?
            s_igbt_limit_samples + 1U : 0U;
#else
        s_igbt_limit_samples = 0;
#endif
        s_bottom_limit_samples = bottom_valid && !bottom_raw_invalid &&
            !bottom_temperature_interface_safe(s_status.bottom_c) ?
            s_bottom_limit_samples + 1U : 0U;

        if (s_igbt_raw_fault_samples >= MCL02M_RAW_SENSOR_FAULT_SAMPLES) {
            fault_locked("E08 IGBT SENSOR");
        } else if (s_bottom_raw_fault_samples >= MCL02M_RAW_SENSOR_FAULT_SAMPLES) {
            fault_locked("E08 BOTTOM SENSOR");
#if MCL02M_IGBT_INTERFACE_CUTOFF_ENABLED
        } else if (s_igbt_limit_samples >=
                   MCL02M_IGBT_INTERFACE_CUTOFF_SAMPLES) {
            fault_locked("IGBT INTERFACE LIMIT");
#endif
        } else if (s_bottom_limit_samples >=
                   MCL02M_BOTTOM_INTERFACE_CUTOFF_SAMPLES) {
            fault_locked("BOTTOM LIMIT");
        }
    } else {
        s_igbt_raw_fault_samples = 0;
        s_bottom_raw_fault_samples = 0;
        s_igbt_limit_samples = 0;
        s_bottom_limit_samples = 0;
    }
    xSemaphoreGive(s_status_lock);
}

static void update_status_feedback(void)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const int64_t now_us = esp_timer_get_time();
    const bool r20_valid = (s_status.valid_mask & (1U << 0)) != 0;
    const bool r26_valid = (s_status.valid_mask & (1U << 6)) != 0;
    const uint8_t r20 = s_status.registers[0];
    const uint8_t r26 = s_status.registers[6];
    if (r20_valid && r26_valid) {
        ++s_status.feedback_sequence;
        s_status.feedback_r20 = r20;
        s_status.feedback_r26 = r26;
        s_status.feedback_gear = 0;
        s_status.feedback_gear_known = false;
        if (r20_known_fault(r20)) {
            s_status.feedback_state = PB_FEEDBACK_FAULT;
        } else if (r20 == 0x02) {
            s_status.feedback_state = PB_FEEDBACK_NO_PAN;
        } else if (r26 == 0) {
            s_status.feedback_state = PB_FEEDBACK_OUTPUT_OFF;
            s_status.feedback_gear_known = true;
        } else if (r26 == 0x01 || r26 == 0x02) {
            s_status.feedback_state = PB_FEEDBACK_SESSION_ACTIVE;
        } else {
            s_status.feedback_state = PB_FEEDBACK_UNKNOWN;
        }
    }
    const bool start_confirmation_open =
        s_status.transition_pending &&
        s_status.transition_kind == PB_TRANSITION_START &&
        s_status.transition_command_transmitted &&
        s_status.feedback_sequence > s_transition_feedback_baseline &&
        s_transition_deadline_us != 0 && now_us < s_transition_deadline_us;
    const bool resume_heating_confirmation_open =
        s_status.transition_pending &&
        (s_status.transition_kind == PB_TRANSITION_RESUME ||
         s_status.transition_kind == PB_TRANSITION_PAN_RETURN_RESUME) &&
        s_status.transition_requested_gear != 0 &&
        s_status.transition_command_transmitted;
    const bool heating_feedback_open =
        s_status.state == PB_STATE_HEATING ||
        (start_confirmation_open && s_status.transition_requested_gear != 0) ||
        resume_heating_confirmation_open;

    if ((s_status.state == PB_STATE_STOPPING || s_status.state == PB_STATE_FAULT) &&
        r26_valid) {
        if (r26 == 0) {
            if (s_stop_zero_samples < MCL02M_STOP_CONFIRM_SAMPLES)
                ++s_stop_zero_samples;
            s_status.stop_confirm_samples = (uint8_t)s_stop_zero_samples;
            if (s_stop_zero_samples >= MCL02M_STOP_CONFIRM_SAMPLES) {
                s_status.stop_verified = true;
                if (s_status.state == PB_STATE_STOPPING) {
                    finish_stop_locked();
                } else {
                    s_status.confirmed_state = PB_STATE_STOPPED;
                    s_status.confirmed_gear = 0;
                    s_status.confirmation_inferred = false;
                    freeze_stop_evidence_locked();
                }
            }
        } else {
            s_stop_zero_samples = 0;
            s_status.stop_confirm_samples = 0;
            s_status.stop_verified = false;
        }
    }

#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    if ((r20_valid || r26_valid) &&
        (!s_feedback_reported || r20 != s_reported_r20 || r26 != s_reported_r26)) {
        ESP_LOGI(TAG, "P,%s,%02X,%02X,%02X,%02X,%02X",
                 powerboard_state_name(s_status.state), r20, r26,
                 s_status.last_command_0d, s_status.last_command_00,
                 s_status.last_command_0c);
        s_feedback_reported = true;
        s_reported_r20 = r20;
        s_reported_r26 = r26;
    }
#endif

    if (r20_valid && r20 == 0x02 && heating_feedback_open) {
        if (++s_no_pan_samples >= MCL02M_NO_PAN_SAMPLES) {
            enter_no_pan_locked("NO PAN");
        }
    } else if (r20_valid && r20 == 0x02 &&
               s_status.transition_pending &&
               (s_status.transition_kind == PB_TRANSITION_PAN_RETURN_HOLD ||
                s_status.transition_kind == PB_TRANSITION_PAN_RETURN_RESUME)) {
        if (++s_no_pan_samples >= MCL02M_NO_PAN_SAMPLES)
            enter_no_pan_locked("PAN LOST");
    } else if (r20_valid && r20_proves_pan_present(r20) && r26_valid &&
               (r26 == 0x01 || r26 == 0x02) &&
               s_status.state == PB_STATE_NO_PAN &&
               !s_status.transition_pending) {
        s_no_pan_samples = 0;
        s_status.cookware_limited = r26 == 0x01;
        begin_transition_locked(PB_TRANSITION_PAN_RETURN_HOLD,
                                PB_STATE_ACTIVE_ZERO, 0);
    } else if (r20_valid && r20 != 0x02) {
        s_no_pan_samples = 0;
    }

    /*
     * Stock ESP32 firmware treats R26=01 as valid small-cookware feedback:
     * it caps the command at gear 35 and forces the 0x80/A1 topology. R26=02
     * is the unrestricted heating acknowledgement. Do not interpret R26=01
     * while paused/at active zero because the retained-session feedback has a
     * different meaning when W00 is zero.
     */
    if (r20_valid && r20_session_compatible(r20) && r26_valid &&
        heating_feedback_open) {
        if (r26 == 0x01) {
            s_status.cookware_limited = true;
            if (s_status.applied_gear > MCL02M_SMALL_COOKWARE_MAX_GEAR)
                s_status.applied_gear = MCL02M_SMALL_COOKWARE_MAX_GEAR;
            if (s_status.applied_gear != 0) s_status.topology = 0xa1;
        } else if (r26 == 0x02) {
            s_status.cookware_limited = false;
        }
    }

    const bool transition_confirmation_open =
        s_status.transition_pending &&
        s_status.transition_command_transmitted &&
        s_status.feedback_sequence > s_transition_feedback_baseline &&
        s_transition_deadline_us != 0 && now_us < s_transition_deadline_us;
    const bool zero_session_transition =
        s_status.transition_kind == PB_TRANSITION_ACTIVE_ZERO ||
        s_status.transition_kind == PB_TRANSITION_PAUSE ||
        s_status.transition_kind == PB_TRANSITION_PAN_RETURN_HOLD ||
        ((s_status.transition_kind == PB_TRANSITION_START ||
          s_status.transition_kind == PB_TRANSITION_RESUME ||
          s_status.transition_kind == PB_TRANSITION_PAN_RETURN_RESUME) &&
         s_status.transition_requested_gear == 0);
    const bool pan_return_transition =
        s_status.transition_kind == PB_TRANSITION_PAN_RETURN_HOLD ||
        s_status.transition_kind == PB_TRANSITION_PAN_RETURN_RESUME;
    const bool transition_r20_compatible = pan_return_transition ?
        r20_proves_pan_present(r20) :
        (r20_session_compatible(r20) ||
         (r20 == 0x02 && zero_session_transition));
    if (transition_confirmation_open && r20_valid && transition_r20_compatible &&
        r26_valid && (r26 == 0x01 || r26 == 0x02)) {
        finish_transition_locked();
    }

    if (r20_valid && r20_known_fault(r20)) {
        if (r20 != s_r20_fault_value) {
            s_r20_fault_value = r20;
            s_r20_fault_samples = 1;
        } else {
            ++s_r20_fault_samples;
        }
        s_unknown_r20_present = false;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
        ESP_LOGW(TAG, "E,%s,%02X,%02X,%u/%u",
                 powerboard_state_name(s_status.state), r20, r26,
                 s_r20_fault_samples, MCL02M_KNOWN_R20_FAULT_SAMPLES);
#endif
        if (s_r20_fault_samples >= MCL02M_KNOWN_R20_FAULT_SAMPLES &&
            state_session_open(s_status.state)) {
            fault_locked(r20_fault_name(r20));
        } else if (s_r20_fault_samples >= MCL02M_KNOWN_R20_FAULT_SAMPLES &&
                   s_status.state == PB_STATE_STOPPING) {
            record_stop_issue_locked(r20_fault_name(r20));
        }
    } else if (r20_valid && !r20_silent_nonfault(r20)) {
        s_r20_fault_samples = 0;
        if (!s_unknown_r20_present || r20 != s_unknown_r20_present_value) {
            s_unknown_r20_present = true;
            s_unknown_r20_present_value = r20;
            s_status.unknown_r20_value = r20;
            ++s_status.unknown_r20_seq;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
            ESP_LOGW(TAG, "W,%s,R20,%02X,R26,%02X,SEQ,%" PRIu32,
                     powerboard_state_name(s_status.state), r20, r26,
                     s_status.unknown_r20_seq);
#endif
        }
    } else {
        s_r20_fault_samples = 0;
        s_unknown_r20_present = false;
    }

    if (s_status.state == PB_STATE_HEARTBEAT_GAP && r26_valid && r26 == 0) {
        s_status.heartbeat_gap_observed_stop = true;
    }

    if ((s_status.state == PB_STATE_STOPPED || s_status.state == PB_STATE_ARMED) &&
        r26_valid && r26 != 0) {
        if (++s_stop_active_samples >= 4) fault_locked("STOP VERIFY");
    } else {
        s_stop_active_samples = 0;
    }
    if (s_status.state != PB_STATE_STOPPING && s_status.state != PB_STATE_FAULT &&
        r26_valid)
        s_status.stop_verified = r26 == 0;
    xSemaphoreGive(s_status_lock);
}

static uint8_t next_ramped_gear(uint8_t current, uint8_t target)
{
    int candidate;
    if (target > current) {
        candidate = current + MCL02M_GEAR_STEP_PER_HEARTBEAT;
        if (candidate > target) candidate = target;
        if (target >= MCL02M_HIGH_TOPOLOGY_MIN_GEAR &&
            current <= MCL02M_LOW_TOPOLOGY_MAX_GEAR &&
            candidate > MCL02M_LOW_TOPOLOGY_MAX_GEAR &&
            candidate < MCL02M_HIGH_TOPOLOGY_MIN_GEAR) {
            candidate = MCL02M_HIGH_TOPOLOGY_MIN_GEAR;
        }
    } else {
        candidate = current - (int)MCL02M_GEAR_STEP_PER_HEARTBEAT;
        if (candidate < target) candidate = target;
        if (target <= MCL02M_LOW_TOPOLOGY_MAX_GEAR &&
            current >= MCL02M_HIGH_TOPOLOGY_MIN_GEAR &&
            candidate > MCL02M_LOW_TOPOLOGY_MAX_GEAR &&
            candidate < MCL02M_HIGH_TOPOLOGY_MIN_GEAR) {
            candidate = MCL02M_LOW_TOPOLOGY_MAX_GEAR;
        }
    }
    return (uint8_t)candidate;
}

static uint8_t cold_start_first_gear(uint8_t target)
{
    /*
     * Select the final target's relay topology in the very first nonzero
     * command, then ramp only inside that topology. This preserves a gentle
     * cold start without forcing the power board through one or two needless
     * relay/IGBT transitions on the way to the requested range.
     */
    if (target <= MCL02M_LOW_START_RAMP_GEAR) return target;
    if (target <= MCL02M_LOW_TOPOLOGY_MAX_GEAR)
        return MCL02M_LOW_START_RAMP_GEAR;
    if (target < MCL02M_HIGH_TOPOLOGY_MIN_GEAR)
        return MCL02M_MID_TOPOLOGY_MIN_GEAR;
    return MCL02M_HIGH_TOPOLOGY_MIN_GEAR;
}

static uint8_t retained_resume_first_gear(uint8_t target)
{
    /*
     * The power session and relay topology remain established in active zero.
     * Resume therefore sends the freshly requested effective gear directly,
     * instead of injecting a cold-start gear-10 ramp that could add topology
     * switching. Temperature mode computes this target immediately beforehand.
     */
    return target;
}

static void advance_ramp(void)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const uint8_t effective_target = cookware_limited_gear(
        s_status.target_gear, s_status.cookware_limited);
    if (s_status.transition_pending ||
        (s_status.state != PB_STATE_STARTING && s_status.state != PB_STATE_HEATING) ||
        s_status.applied_gear == effective_target) {
        xSemaphoreGive(s_status_lock);
        return;
    }
    const uint8_t candidate = next_ramped_gear(s_status.applied_gear,
                                                effective_target);
    s_status.applied_gear = candidate;
    s_status.topology = topology_for_gear(candidate);
    xSemaphoreGive(s_status_lock);
}

static void emit_status(void)
{
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    powerboard_status_t s;
    powerboard_control_get_status(&s);
    ESP_LOGI(TAG,
             "D,%s,%u,%u,%02X,%02X,%02X,%02X,"
             "%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,"
             "%04X,%u,%u,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
             "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
             "%" PRIu32 ",%u,%u,%u,%u,%02X,%02X,%" PRIu32 ",%s,"
             "%" PRIu32 ",%u,%u,%u,%" PRIu32 ",%s,%s,"
             "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%u,%u,"
             "%" PRIu32 ",%u,%u,%u,%s,%u,%s,%u,%02X,"
             "%" PRIu32 ",%s,%u,%u,%" PRIu32 ",%s,%02X,%02X,%u,"
             "%" PRIu32 ",%s",
             powerboard_state_name(s.state), s.target_gear, s.applied_gear, s.topology,
             s.last_command_0d, s.last_command_00, s.last_command_0c,
             s.registers[0], s.registers[1], s.registers[2], s.registers[3],
             s.registers[4], s.registers[5], s.registers[6], s.registers[7],
             s.valid_mask, s.igbt_c, s.bottom_c, s.run_elapsed_ms,
             s.run_remaining_ms, s.arm_remaining_ms, s.start_confirm_remaining_ms,
             s.heartbeat_gap_remaining_ms, s.completed_cycles, s.bad_cycles,
             s.consecutive_bad_cycles, s.active_zero_entries, s.active_zero_resumes,
             s.stop_verified ? 1U : 0U,
             s.heartbeat_gap_observed_stop ? 1U : 0U,
             MCL02M_ACTIVE_ZERO_ENABLED ? 1U : 0U,
             s.cookware_limited ? 1U : 0U, s.registers[8],
             s.unknown_r20_value, s.unknown_r20_seq, s.fault,
             s.stop_generation, s.stop_verified ? 1U : 0U,
             s.stop_confirm_samples, s.stop_timed_out ? 1U : 0U,
             s.stop_elapsed_ms, s.stop_reason, s.stop_issue,
             s.lease_generation, s.lease_remaining_ms, s.lease_renewals,
             s.lease_active ? 1U : 0U, s.lease_expired ? 1U : 0U,
             s.transition_generation, s.transition_kind,
             s.transition_pending ? 1U : 0U,
             s.transition_command_transmitted ? 1U : 0U,
             powerboard_state_name(s.transition_requested_state),
             s.transition_requested_gear,
             powerboard_state_name(s.transmitted_state), s.transmitted_gear,
             s.transmitted_topology, s.transition_confirmed_generation,
             powerboard_state_name(s.confirmed_state), s.confirmed_gear,
             s.confirmation_inferred ? 1U : 0U, s.feedback_sequence,
             powerboard_feedback_state_name(s.feedback_state),
             s.feedback_r20, s.feedback_r26,
             s.feedback_gear_known ? 1U : 0U,
             s.transition_rejection_sequence, s.transition_rejection);
#endif
#if !MCL02M_COMPACT_UART_TELEMETRY
    static char json[4096];
    powerboard_control_status_json(json, sizeof(json));
    telemetry_emit(json);
#endif
}

static void control_task(void *arg)
{
    (void)arg;
    const esp_err_t watchdog = esp_task_wdt_add(NULL);
    esp_err_t boot_stop = send_stop_sequence(NULL, NULL);
    esp_err_t startup = startup_probe();
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (watchdog == ESP_OK && boot_stop == ESP_OK && startup == ESP_OK) {
        s_status.state = PB_STATE_STOPPED;
        s_status.confirmed_state = PB_STATE_STOPPED;
        strlcpy(s_status.fault, "NONE", sizeof(s_status.fault));
    } else {
        fault_locked(watchdog == ESP_OK ? "BOOT I2C" : "TASK WDT");
    }
    xSemaphoreGive(s_status_lock);
    if (watchdog == ESP_OK) esp_task_wdt_reset();

    static const uint8_t normal_read_order[] = {
        0x26,0x27,0x20,0x21,0x22,0x23,0x24,0x25
    };
    static const uint8_t recovery_read_order[] = {
        0x26,0x20,0x22,0x23,0x24
    };
    for (;;) {
        const TickType_t cycle_start = xTaskGetTickCount();
        uint16_t read_attempt_mask = 0;
        uint16_t read_error_mask = 0;
        uint8_t write_attempt_mask = 0;
        uint8_t write_error_mask = 0;
        bool command_expected = false;
        bool interrupted = false;

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        const bool recovery_cycle = s_status.i2c_recovery_active;
        xSemaphoreGive(s_status_lock);
        const uint8_t *read_order = recovery_cycle ?
            recovery_read_order : normal_read_order;
        const size_t read_count = recovery_cycle ?
            sizeof(recovery_read_order) : sizeof(normal_read_order);
        const unsigned read_slot_ms = recovery_cycle ?
            PB_RECOVERY_READ_SLOT_MS : 50U;
        const unsigned command_0d_ms = recovery_cycle ?
            PB_RECOVERY_COMMAND_0D_MS : 400U;
        const unsigned command_00_ms = recovery_cycle ?
            PB_RECOVERY_COMMAND_00_MS : 450U;
        const unsigned command_0c_ms = recovery_cycle ?
            PB_RECOVERY_COMMAND_0C_MS : 454U;

        for (size_t i = 0; i < read_count; ++i) {
            if (i != 0 && !wait_until_or_stop(
                    cycle_start, (unsigned)i * read_slot_ms)) {
                interrupted = true;
                break;
            }
            const uint16_t reg_mask = PB_REG_MASK(read_order[i]);
            read_attempt_mask |= reg_mask;
            if (read_and_store(read_order[i]) != ESP_OK)
                read_error_mask |= reg_mask;
        }

        update_status_feedback();
        update_time_and_safety(esp_timer_get_time());

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        /* A latched fault retransmits the complete Stop frame every heartbeat. */
        if (s_force_stop || s_status.state == PB_STATE_FAULT) interrupted = true;
        xSemaphoreGive(s_status_lock);

        if (!interrupted && !wait_until_or_stop(cycle_start, command_0d_ms))
            interrupted = true;

        if (!interrupted) {
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            const powerboard_state_t command_state = s_status.state;
            const uint8_t gear = s_status.applied_gear;
            const uint8_t target_gear = cookware_limited_gear(
                s_status.target_gear, s_status.cookware_limited);
            const uint8_t topology = s_status.topology;
            const bool transition_pending = s_status.transition_pending;
            const powerboard_transition_t transition_kind = s_status.transition_kind;
            const uint8_t transition_requested_gear =
                cookware_limited_gear(s_status.transition_requested_gear,
                                      s_status.cookware_limited);
            const uint32_t transition_generation = s_status.transition_generation;
            xSemaphoreGive(s_status_lock);

            if (command_state != PB_STATE_HEARTBEAT_GAP) {
                command_expected = true;
                uint8_t command_0d = 0;
                uint8_t command_00 = 0;
                uint8_t command_0c = 0;
                bool command_transmitted = true;
                if (transition_pending &&
                           (transition_kind == PB_TRANSITION_ACTIVE_ZERO ||
                            transition_kind == PB_TRANSITION_PAUSE ||
                            transition_kind == PB_TRANSITION_PAN_RETURN_HOLD ||
                            ((transition_kind == PB_TRANSITION_START ||
                              transition_kind == PB_TRANSITION_RESUME ||
                              transition_kind == PB_TRANSITION_PAN_RETURN_RESUME) &&
                             transition_requested_gear == 0))) {
#if MCL02M_ACTIVE_ZERO_ENABLED
                    command_0d = PB_ACTIVE_ZERO_0D;
#endif
                } else if (command_state == PB_STATE_NO_PAN) {
                    command_0d = 0x80;
                    command_00 = 1;
                    command_0c = target_gear;
                } else if (transition_pending &&
                           (transition_kind == PB_TRANSITION_START ||
                            transition_kind == PB_TRANSITION_RESUME ||
                            transition_kind == PB_TRANSITION_PAN_RETURN_RESUME)) {
                    command_0d = topology_for_gear(transition_requested_gear);
                    command_00 = 1;
                    command_0c = transition_requested_gear;
                } else if (command_state == PB_STATE_STARTING ||
                           command_state == PB_STATE_HEATING) {
                    command_0d = topology;
                    command_00 = 1;
                    command_0c = gear;
                } else if (command_state == PB_STATE_ACTIVE_ZERO ||
                           command_state == PB_STATE_PAUSED) {
#if MCL02M_ACTIVE_ZERO_ENABLED
                    command_0d = PB_ACTIVE_ZERO_0D;
#endif
                }
                xSemaphoreTake(s_status_lock, portMAX_DELAY);
                s_status.last_command_0d = command_0d;
                s_status.last_command_00 = command_00;
                s_status.last_command_0c = command_0c;
                xSemaphoreGive(s_status_lock);
                write_attempt_mask |= PB_WRITE_0D_MASK;
                if (write_register(0x0d, command_0d) != ESP_OK) {
                    write_error_mask |= PB_WRITE_0D_MASK;
                    command_transmitted = false;
                }
                if (!wait_until_or_stop(cycle_start, command_00_ms)) {
                    interrupted = true;
                } else {
                    write_attempt_mask |= PB_WRITE_00_MASK;
                    if (write_register(0x00, command_00) != ESP_OK) {
                        write_error_mask |= PB_WRITE_00_MASK;
                        command_transmitted = false;
                    }
                    if (!wait_until_or_stop(cycle_start, command_0c_ms)) {
                        interrupted = true;
                    } else {
                        write_attempt_mask |= PB_WRITE_0C_MASK;
                        if (write_register(0x0c, command_0c) != ESP_OK) {
                            write_error_mask |= PB_WRITE_0C_MASK;
                            command_transmitted = false;
                        }
                    }
                }

                if (!interrupted && command_transmitted) {
                    bool transition_wait_started = false;
                    xSemaphoreTake(s_status_lock, portMAX_DELAY);
                    s_last_successful_command_0d = command_0d;
                    s_last_successful_command_0c = command_0c;
                    if (command_0d == PB_ACTIVE_ZERO_0D && command_00 == 0 &&
                        command_0c == 0) {
                        s_status.transmitted_state =
                            transition_pending && transition_kind == PB_TRANSITION_PAUSE ?
                            PB_STATE_PAUSED : PB_STATE_ACTIVE_ZERO;
                    } else if (command_state == PB_STATE_NO_PAN) {
                        s_status.transmitted_state = PB_STATE_NO_PAN;
                    } else if (command_00 == 1) {
                        s_status.transmitted_state = PB_STATE_HEATING;
                    } else {
                        s_status.transmitted_state = PB_STATE_STOPPED;
                    }
                    s_status.transmitted_gear = command_0c;
                    s_status.transmitted_topology = command_0d;
                    if (s_status.transition_pending &&
                        s_status.transition_generation == transition_generation) {
                        const bool zero_transition =
                            transition_kind == PB_TRANSITION_ACTIVE_ZERO ||
                            transition_kind == PB_TRANSITION_PAUSE ||
                            transition_kind == PB_TRANSITION_PAN_RETURN_HOLD ||
                            transition_requested_gear == 0;
                        const bool command_matches = zero_transition ?
                            (command_0d == PB_ACTIVE_ZERO_0D && command_00 == 0 &&
                             command_0c == 0) :
                            (command_0d == topology_for_gear(transition_requested_gear) &&
                             command_00 == 1 &&
                             command_0c == transition_requested_gear);
                        if (command_matches) {
                            s_status.transition_command_transmitted = true;
                            s_transition_feedback_baseline = s_status.feedback_sequence;
                            if (s_transition_deadline_us == 0) {
                                const uint32_t timeout_ms =
                                    transition_kind == PB_TRANSITION_START ?
                                    MCL02M_START_CONFIRM_TIMEOUT_MS :
                                    MCL02M_TRANSITION_CONFIRM_TIMEOUT_MS;
                                s_transition_deadline_us = esp_timer_get_time() +
                                    (int64_t)timeout_ms * 1000;
                                if (transition_kind == PB_TRANSITION_START)
                                    s_start_confirm_deadline_us = s_transition_deadline_us;
                                transition_wait_started = true;
                            }
                        }
                    }
                    xSemaphoreGive(s_status_lock);
                    if (transition_wait_started) {
                        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"transition_wait\","
                                        "\"generation\":%" PRIu32 ","
                                        "\"kind\":\"%s\"}",
                                        esp_timer_get_time() / 1000,
                                        transition_generation,
                                        powerboard_transition_name(transition_kind));
                    }
                }
            } else {
                telemetry_emitf("{\"t_ms\":%lld,\"type\":\"pb_heartbeat_gap_cycle\"}",
                                esp_timer_get_time() / 1000);
            }
        }

        if (interrupted) {
            command_expected = true;
            const esp_err_t stop_err = send_stop_sequence(
                &write_attempt_mask, &write_error_mask);
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            /* Retry an unsuccessful safety Stop on the next heartbeat. */
            s_force_stop = stop_err != ESP_OK;
            xSemaphoreGive(s_status_lock);
        }

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        update_i2c_health_locked(esp_timer_get_time(),
                                 read_attempt_mask, read_error_mask,
                                 command_expected, write_attempt_mask,
                                 write_error_mask);
        const bool next_recovery_cycle = s_status.i2c_recovery_active;
        xSemaphoreGive(s_status_lock);

        advance_ramp();
        emit_status();
        if (watchdog == ESP_OK) esp_task_wdt_reset();

        TickType_t next = cycle_start;
        const unsigned next_period_ms = next_recovery_cycle ?
            MCL02M_I2C_RECOVERY_HEARTBEAT_MS : MCL02M_CONTROL_HEARTBEAT_MS;
        vTaskDelayUntil(&next, pdMS_TO_TICKS(next_period_ms));
    }
}

esp_err_t powerboard_control_init(void)
{
    s_bus_lock = xSemaphoreCreateMutex();
    s_status_lock = xSemaphoreCreateMutex();
    if (s_bus_lock == NULL || s_status_lock == NULL) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = PB_STATE_BOOT;
    s_status.confirmed_state = PB_STATE_BOOT;
    s_status.feedback_state = PB_FEEDBACK_UNKNOWN;
    strlcpy(s_status.fault, "BOOT", sizeof(s_status.fault));
    strlcpy(s_status.stop_reason, "BOOT", sizeof(s_status.stop_reason));
    strlcpy(s_status.stop_issue, "NONE", sizeof(s_status.stop_issue));
    strlcpy(s_status.transition_result, "NONE",
            sizeof(s_status.transition_result));
    strlcpy(s_status.transition_rejection, "NONE",
            sizeof(s_status.transition_rejection));

    esp_err_t err = bus_init();
    if (err != ESP_OK) return err;
    if (xTaskCreate(control_task, "pb_control", 6144, NULL, 9, &s_control_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void powerboard_control_get_status(powerboard_status_t *status)
{
    if (status == NULL || s_status_lock == NULL) return;
    const int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    *status = s_status;
    status->arm_remaining_ms = remaining_ms(s_arm_deadline_us, now);
    status->start_confirm_remaining_ms = remaining_ms(s_start_confirm_deadline_us, now);
    status->stop_confirm_remaining_ms = remaining_ms(s_stop_confirm_deadline_us, now);
    status->lease_remaining_ms = remaining_ms(s_lease_deadline_us, now);
    status->transition_remaining_ms = remaining_ms(s_transition_deadline_us, now);
    status->heartbeat_gap_remaining_ms = remaining_ms(s_heartbeat_gap_deadline_us, now);
    status->i2c_critical_loss_ms = elapsed_ms(s_i2c_critical_bad_since_us, now);
    status->i2c_command_loss_ms = elapsed_ms(s_i2c_command_bad_since_us, now);
    if (s_stop_started_us != 0 && now >= s_stop_started_us)
        status->stop_elapsed_ms = (uint32_t)((now - s_stop_started_us) / 1000);
    if (s_run_started_us != 0 && now >= s_run_started_us) {
        status->run_elapsed_ms = (uint32_t)((now - s_run_started_us) / 1000);
    }
    status->run_remaining_ms = remaining_ms(s_run_deadline_us, now);
    xSemaphoreGive(s_status_lock);
}

size_t powerboard_control_status_json(char *output, size_t output_size)
{
    if (output == NULL || output_size == 0) return 0;
    powerboard_status_t s;
    powerboard_control_get_status(&s);
    return (size_t)snprintf(output, output_size,
        "{\"t_ms\":%lld,\"type\":\"power_control\",\"state\":\"%s\","
        "\"gear_target\":%u,\"gear_applied\":%u,\"topology\":%u,"
        "\"cmd_0d\":%u,\"cmd_00\":%u,\"cmd_0c\":%u,"
        "\"r20\":%u,\"r21\":%u,\"r22\":%u,\"r23\":%u,\"r24\":%u,"
        "\"r25\":%u,\"r26\":%u,\"r27\":%u,\"r28\":%u,\"r29\":%u,"
        "\"r2a\":%u,\"r2b\":%u,\"r2c\":%u,\"r2d\":%u,\"r2e\":%u,\"r2f\":%u,"
        "\"igbt_c\":%u,\"bottom_c\":%u,\"valid_mask\":%u,"
        "\"startup_required_mask\":%u,\"startup_service_mask\":%u,"
        "\"startup_required_ok\":%s,\"startup_service_failures\":%u,"
        "\"run_ms\":%" PRIu32 ",\"remaining_ms\":%" PRIu32 ","
        "\"arm_ms\":%" PRIu32 ",\"start_confirm_ms\":%" PRIu32 ","
        "\"stop_elapsed_ms\":%" PRIu32 ",\"stop_confirm_ms\":%" PRIu32 ","
        "\"stop_generation\":%" PRIu32 ",\"stop_confirm_samples\":%u,"
        "\"stop_verified\":%s,\"stop_timed_out\":%s,"
        "\"stop_reason\":\"%s\",\"stop_issue\":\"%s\","
        "\"lease_active\":%s,\"lease_expired\":%s,"
        "\"lease_remaining_ms\":%" PRIu32 ","
        "\"lease_generation\":%" PRIu32 ",\"lease_renewals\":%" PRIu32 ","
        "\"lease_expirations\":%" PRIu32 ","
        "\"transition_pending\":%s,\"transition_kind\":\"%s\","
        "\"transition_generation\":%" PRIu32 ","
        "\"transition_confirmed_generation\":%" PRIu32 ","
        "\"transition_rejection_sequence\":%" PRIu32 ","
        "\"transition_remaining_ms\":%" PRIu32 ","
        "\"transition_command_transmitted\":%s,"
        "\"requested_state\":\"%s\",\"requested_gear\":%u,"
        "\"transmitted_state\":\"%s\",\"transmitted_gear\":%u,"
        "\"transmitted_topology\":%u,"
        "\"confirmed_state\":\"%s\",\"confirmed_gear\":%u,"
        "\"confirmation_inferred\":%s,\"transition_result\":\"%s\","
        "\"transition_rejection\":\"%s\","
        "\"feedback_sequence\":%" PRIu32 ",\"feedback_state\":\"%s\","
        "\"feedback_r20\":%u,\"feedback_r26\":%u,"
        "\"feedback_gear\":%u,\"feedback_gear_known\":%s,"
        "\"hb_gap_ms\":%" PRIu32 ","
        "\"hb_gap_observed_stop\":%s,\"cycles\":%" PRIu32 ",\"bad_cycles\":%" PRIu32 ","
        "\"consecutive_bad_cycles\":%" PRIu32 ","
        "\"critical_bad_cycles\":%" PRIu32 ",\"service_bad_cycles\":%" PRIu32 ","
        "\"i2c_recovery_active\":%s,\"i2c_recovery_good_cycles\":%u,"
        "\"i2c_recovery_entries\":%" PRIu32 ","
        "\"i2c_critical_loss_ms\":%" PRIu32 ",\"i2c_command_loss_ms\":%" PRIu32 ","
        "\"i2c_read_error_mask\":%u,\"i2c_write_error_mask\":%u,"
        "\"active_zero_entries\":%" PRIu32 ",\"active_zero_resumes\":%" PRIu32 ","
        "\"active_zero_enabled\":%s,\"cookware_limited\":%s,"
        "\"unknown_r20_value\":%u,\"unknown_r20_seq\":%" PRIu32 ",\"fault\":\"%s\","
        "\"start_incident\":{\"valid\":%s,\"sequence\":%" PRIu32 ","
        "\"t_ms\":%" PRIu64 ",\"state\":\"%s\",\"r20\":%u,\"r26\":%u,"
        "\"r28\":%u,\"valid_mask\":%u,\"requested_gear\":%u,"
        "\"transmitted_gear\":%u,\"transmitted_topology\":%u,"
        "\"cmd_0d\":%u,\"cmd_00\":%u,\"cmd_0c\":%u,"
        "\"cycles\":%" PRIu32 ",\"bad_cycles\":%" PRIu32 ","
        "\"consecutive_bad_cycles\":%" PRIu32 ",\"reason\":\"%s\"},"
        "\"i2c_incident\":{\"valid\":%s,\"sequence\":%" PRIu32 ","
        "\"t_ms\":%" PRIu64 ",\"state\":\"%s\","
        "\"read_error_mask\":%u,\"write_error_mask\":%u,\"valid_mask\":%u,"
        "\"r20\":%u,\"r26\":%u,\"cmd_0d\":%u,\"cmd_00\":%u,\"cmd_0c\":%u,"
        "\"cycles\":%" PRIu32 ",\"bad_cycles\":%" PRIu32 ","
        "\"consecutive_bad_cycles\":%" PRIu32 ","
        "\"critical_loss_ms\":%" PRIu32 ",\"command_loss_ms\":%" PRIu32 ","
        "\"recovery_entries\":%" PRIu32 ",\"reason\":\"%s\"}}",
        esp_timer_get_time() / 1000, powerboard_state_name(s.state),
        s.target_gear, s.applied_gear, s.topology,
        s.last_command_0d, s.last_command_00, s.last_command_0c,
        s.registers[0], s.registers[1], s.registers[2], s.registers[3],
        s.registers[4], s.registers[5], s.registers[6], s.registers[7],
        s.registers[8], s.registers[9], s.registers[10], s.registers[11],
        s.registers[12], s.registers[13], s.registers[14], s.registers[15],
        s.igbt_c, s.bottom_c, s.valid_mask,
        s.startup_required_valid_mask, s.startup_service_valid_mask,
        s.startup_required_ok ? "true" : "false", s.startup_service_failures,
        s.run_elapsed_ms, s.run_remaining_ms, s.arm_remaining_ms,
        s.start_confirm_remaining_ms, s.stop_elapsed_ms,
        s.stop_confirm_remaining_ms, s.stop_generation, s.stop_confirm_samples,
        s.stop_verified ? "true" : "false",
        s.stop_timed_out ? "true" : "false", s.stop_reason, s.stop_issue,
        s.lease_active ? "true" : "false", s.lease_expired ? "true" : "false",
        s.lease_remaining_ms, s.lease_generation, s.lease_renewals,
        s.lease_expirations,
        s.transition_pending ? "true" : "false",
        powerboard_transition_name(s.transition_kind), s.transition_generation,
        s.transition_confirmed_generation, s.transition_rejection_sequence,
        s.transition_remaining_ms,
        s.transition_command_transmitted ? "true" : "false",
        powerboard_state_name(s.transition_requested_state),
        s.transition_requested_gear, powerboard_state_name(s.transmitted_state),
        s.transmitted_gear, s.transmitted_topology,
        powerboard_state_name(s.confirmed_state), s.confirmed_gear,
        s.confirmation_inferred ? "true" : "false", s.transition_result,
        s.transition_rejection,
        s.feedback_sequence, powerboard_feedback_state_name(s.feedback_state),
        s.feedback_r20, s.feedback_r26, s.feedback_gear,
        s.feedback_gear_known ? "true" : "false",
        s.heartbeat_gap_remaining_ms,
        s.heartbeat_gap_observed_stop ? "true" : "false", s.completed_cycles,
        s.bad_cycles, s.consecutive_bad_cycles,
        s.critical_bad_cycles, s.service_bad_cycles,
        s.i2c_recovery_active ? "true" : "false",
        s.i2c_recovery_good_cycles, s.i2c_recovery_entries,
        s.i2c_critical_loss_ms, s.i2c_command_loss_ms,
        s.last_i2c_read_error_mask, s.last_i2c_write_error_mask,
        s.active_zero_entries, s.active_zero_resumes,
        MCL02M_ACTIVE_ZERO_ENABLED ? "true" : "false",
        s.cookware_limited ? "true" : "false", s.unknown_r20_value,
        s.unknown_r20_seq, s.fault,
        s.start_incident.valid ? "true" : "false", s.start_incident.sequence,
        s.start_incident.timestamp_ms,
        powerboard_state_name(s.start_incident.lower_state),
        s.start_incident.r20, s.start_incident.r26, s.start_incident.r28,
        s.start_incident.valid_mask, s.start_incident.requested_gear,
        s.start_incident.transmitted_gear,
        s.start_incident.transmitted_topology, s.start_incident.command_0d,
        s.start_incident.command_00, s.start_incident.command_0c,
        s.start_incident.completed_cycles, s.start_incident.bad_cycles,
        s.start_incident.consecutive_bad_cycles, s.start_incident.reason,
        s.i2c_incident.valid ? "true" : "false", s.i2c_incident.sequence,
        s.i2c_incident.timestamp_ms,
        powerboard_state_name(s.i2c_incident.lower_state),
        s.i2c_incident.read_error_mask, s.i2c_incident.write_error_mask,
        s.i2c_incident.valid_mask, s.i2c_incident.r20, s.i2c_incident.r26,
        s.i2c_incident.command_0d, s.i2c_incident.command_00,
        s.i2c_incident.command_0c, s.i2c_incident.completed_cycles,
        s.i2c_incident.bad_cycles, s.i2c_incident.consecutive_bad_cycles,
        s.i2c_incident.critical_loss_ms, s.i2c_incident.command_loss_ms,
        s.i2c_incident.recovery_entries, s.i2c_incident.reason);
}

esp_err_t powerboard_control_arm(unsigned window_ms)
{
    if (window_ms < 5000 || window_ms > MCL02M_ARM_WINDOW_MS) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const char *preflight_issue = preflight_issue_locked();
    if (s_status.state != PB_STATE_STOPPED || preflight_issue != NULL) {
        if (s_status.state != PB_STATE_STOPPED) {
            reject_transition_locked("ARM STATE");
        } else {
            char reason[32];
            snprintf(reason, sizeof(reason), "ARM %s", preflight_issue);
            reject_transition_locked(reason);
        }
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = PB_STATE_ARMED;
    s_arm_deadline_us = esp_timer_get_time() + (int64_t)window_ms * 1000;
    s_start_confirm_deadline_us = 0;
    strlcpy(s_status.fault, "NONE", sizeof(s_status.fault));
    xSemaphoreGive(s_status_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"power_arm\",\"window_ms\":%u}",
                    esp_timer_get_time() / 1000, window_ms);
    return ESP_OK;
}

esp_err_t powerboard_control_start(unsigned gear, unsigned duration_ms)
{
    if (gear > MCL02M_MAX_GEAR || duration_ms < 1000 ||
        duration_ms > MCL02M_MAX_RUN_MS) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    const char *preflight_issue = preflight_issue_locked();
#if MCL02M_COOKING_LEASE_ENABLED
    const bool lease_ready = s_status.lease_active && s_lease_deadline_us > now;
#else
    const bool lease_ready = true;
#endif
    if (s_status.state != PB_STATE_ARMED || s_arm_deadline_us <= now ||
        !lease_ready || preflight_issue != NULL) {
        if (s_status.state != PB_STATE_ARMED)
            reject_transition_locked("START NOT ARMED");
        else if (s_arm_deadline_us <= now)
            reject_transition_locked("START ARM EXPIRED");
        else if (!lease_ready)
            reject_transition_locked("START LEASE MISSING");
        else {
            char reason[32];
            snprintf(reason, sizeof(reason), "START %s", preflight_issue);
            reject_transition_locked(reason);
        }
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.target_gear = (uint8_t)gear;
    memset(&s_status.start_incident, 0, sizeof(s_status.start_incident));
    reset_i2c_health_locked(true);
    s_last_successful_command_0d = 0;
    s_last_successful_command_0c = 0;
    const uint8_t first_gear = cold_start_first_gear((uint8_t)gear);
    s_status.applied_gear = 0;
    s_status.topology = 0;
    s_status.cookware_limited = false;
    s_status.state = PB_STATE_STARTING;
    begin_transition_locked(PB_TRANSITION_START,
                            gear == 0 ? PB_STATE_ACTIVE_ZERO : PB_STATE_HEATING,
                            first_gear);
    s_status.heartbeat_gap_observed_stop = false;
    s_run_started_us = now;
    s_run_deadline_us = now + (int64_t)duration_ms * 1000;
    s_arm_deadline_us = 0;
    s_start_confirm_deadline_us = 0;
    strlcpy(s_status.fault, "NONE", sizeof(s_status.fault));
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"power_start\",\"gear\":%u,"
                    "\"duration_ms\":%u}", esp_timer_get_time() / 1000, gear, duration_ms);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    if (gear == 0) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"active_zero_request\","
                        "\"source\":\"start\",\"cmd_0d\":%u}",
                        esp_timer_get_time() / 1000, PB_ACTIVE_ZERO_0D);
        ESP_LOGI(TAG, "Z,REQ,START,81,00,00");
    }
#endif
    return ESP_OK;
}

esp_err_t powerboard_control_set_gear(unsigned gear)
{
    if (gear > MCL02M_MAX_GEAR) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const powerboard_state_t previous = s_status.state;
    if (s_status.transition_pending) {
        if (s_status.transition_kind == PB_TRANSITION_START) {
            if (gear == s_status.target_gear) {
                xSemaphoreGive(s_status_lock);
                return ESP_OK;
            }
            const uint8_t new_target = (uint8_t)gear;
            const uint8_t effective_target = cookware_limited_gear(
                new_target, s_status.cookware_limited);
            const uint8_t first_gear =
                (uint8_t)(effective_target > 10 ? 10 : effective_target);
            s_status.target_gear = new_target;
            begin_transition_locked(PB_TRANSITION_START,
                                    first_gear == 0 ? PB_STATE_ACTIVE_ZERO :
                                                      PB_STATE_HEATING,
                                    first_gear);
            xSemaphoreGive(s_status_lock);
            if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
            ESP_LOGI(TAG, "T,RESTART,%u,%u", new_target, first_gear);
#endif
            return ESP_OK;
        }
        const bool same_zero = gear == 0 &&
            s_status.transition_requested_gear == 0 &&
            (s_status.transition_kind == PB_TRANSITION_ACTIVE_ZERO ||
             s_status.transition_kind == PB_TRANSITION_RESUME);
        const bool same_resume = s_status.transition_kind == PB_TRANSITION_RESUME &&
            gear == s_status.target_gear;
        const bool same = same_zero || same_resume;
        if (!same) reject_transition_locked("GEAR TRANSITION BUSY");
        xSemaphoreGive(s_status_lock);
        return same ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (previous != PB_STATE_STARTING && previous != PB_STATE_HEATING &&
        previous != PB_STATE_NO_PAN && previous != PB_STATE_ACTIVE_ZERO &&
        previous != PB_STATE_PAUSED) {
        reject_transition_locked("GEAR STATE");
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t new_gear = (uint8_t)gear;
    const uint8_t effective_gear = cookware_limited_gear(
        new_gear, s_status.cookware_limited);
    s_status.target_gear = new_gear;
    if (previous == PB_STATE_PAUSED || previous == PB_STATE_NO_PAN) {
        /* Stage the Resume request without claiming that the output changed. */
    } else if (new_gear == 0) {
        if (previous != PB_STATE_ACTIVE_ZERO)
            begin_transition_locked(PB_TRANSITION_ACTIVE_ZERO,
                                    PB_STATE_ACTIVE_ZERO, 0);
    } else if (previous == PB_STATE_ACTIVE_ZERO) {
        const uint8_t first_gear = retained_resume_first_gear(effective_gear);
        begin_transition_locked(PB_TRANSITION_RESUME, PB_STATE_HEATING,
                                first_gear);
    }
    const powerboard_state_t current = s_status.state;
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"power_set_gear\",\"gear\":%u,"
                    "\"from\":\"%s\",\"to\":\"%s\"}",
                    esp_timer_get_time() / 1000, gear,
                    powerboard_state_name(previous),
                    powerboard_state_name(current));
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    if (gear == 0 && previous != PB_STATE_PAUSED) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"active_zero_request\","
                        "\"source\":\"set_gear\",\"from\":\"%s\",\"cmd_0d\":%u}",
                        esp_timer_get_time() / 1000, powerboard_state_name(previous),
                        PB_ACTIVE_ZERO_0D);
        ESP_LOGI(TAG, "Z,REQ,GEAR,81,00,00");
    } else if (gear != 0 && previous == PB_STATE_ACTIVE_ZERO) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"active_zero_resume_request\","
                        "\"gear\":%u,\"topology\":%u}",
                        esp_timer_get_time() / 1000, gear, topology_for_gear(new_gear));
        ESP_LOGI(TAG, "Z,REQ,GEAR,%02X,01,%02X", topology_for_gear(new_gear), new_gear);
    }
#endif
    return ESP_OK;
}

esp_err_t powerboard_control_pause(void)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_status.state == PB_STATE_NO_PAN) {
        reject_transition_locked("PAUSE NO PAN");
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status.transition_pending) {
        const bool same = s_status.transition_kind == PB_TRANSITION_PAUSE;
        const bool replaces_pan_return =
            s_status.transition_kind == PB_TRANSITION_PAN_RETURN_HOLD ||
            s_status.transition_kind == PB_TRANSITION_PAN_RETURN_RESUME;
        if (replaces_pan_return) {
            begin_transition_locked(PB_TRANSITION_PAUSE, PB_STATE_PAUSED, 0);
            xSemaphoreGive(s_status_lock);
            if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
            return ESP_OK;
        }
        if (!same) reject_transition_locked("PAUSE TRANSITION BUSY");
        xSemaphoreGive(s_status_lock);
        return same ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (!state_can_energize(s_status.state) && s_status.state != PB_STATE_ACTIVE_ZERO) {
        reject_transition_locked("PAUSE STATE");
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    begin_transition_locked(PB_TRANSITION_PAUSE, PB_STATE_PAUSED, 0);
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"active_zero_request\","
                    "\"source\":\"manual_pause\",\"cmd_0d\":%u}",
                    esp_timer_get_time() / 1000, PB_ACTIVE_ZERO_0D);
    ESP_LOGI(TAG, "Z,REQ,PAUSE,81,00,00");
#endif
    return ESP_OK;
}

esp_err_t powerboard_control_pan_return_resume(unsigned gear)
{
    if (gear > MCL02M_MAX_GEAR) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    const char *retained_issue = retained_session_issue_locked();
    const bool hold_confirmed =
        !s_status.transition_pending &&
        s_status.transition_kind == PB_TRANSITION_PAN_RETURN_HOLD &&
        s_status.transition_confirmed_generation == s_status.transition_generation;
    if (!hold_confirmed || s_status.state != PB_STATE_ACTIVE_ZERO ||
        s_run_deadline_us <= now || retained_issue != NULL ||
        !r20_proves_pan_present(s_status.registers[0])) {
        if (!hold_confirmed || s_status.state != PB_STATE_ACTIVE_ZERO)
            reject_transition_locked("PAN RETURN STATE");
        else if (s_run_deadline_us <= now)
            reject_transition_locked("PAN RETURN EXPIRED");
        else if (!r20_proves_pan_present(s_status.registers[0]))
            reject_transition_locked("PAN RETURN R20");
        else {
            char reason[32];
            snprintf(reason, sizeof(reason), "PAN RETURN %s", retained_issue);
            reject_transition_locked(reason);
        }
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t requested = (uint8_t)gear;
    const uint8_t effective = cookware_limited_gear(
        requested, s_status.cookware_limited);
    s_status.target_gear = requested;
    begin_transition_locked(PB_TRANSITION_PAN_RETURN_RESUME,
                            effective == 0 ? PB_STATE_ACTIVE_ZERO : PB_STATE_HEATING,
                            effective);
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG, "N,RESUME,%02X,01,%02X", topology_for_gear(effective), effective);
#endif
    return ESP_OK;
}

esp_err_t powerboard_control_resume(void)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    const char *retained_issue = retained_session_issue_locked();
    if (s_status.transition_pending) {
        const bool same = s_status.transition_kind == PB_TRANSITION_RESUME;
        if (!same) reject_transition_locked("RESUME TRANSITION BUSY");
        xSemaphoreGive(s_status_lock);
        return same ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (s_status.state != PB_STATE_PAUSED || s_run_deadline_us <= now ||
        retained_issue != NULL) {
        if (s_status.state != PB_STATE_PAUSED)
            reject_transition_locked("RESUME STATE");
        else if (s_run_deadline_us <= now)
            reject_transition_locked("RESUME RUN EXPIRED");
        else {
            char reason[32];
            snprintf(reason, sizeof(reason), "RESUME %s", retained_issue);
            reject_transition_locked(reason);
        }
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
        ESP_LOGW(TAG, "Z,REJECT,PAUSE,%s,%02X,%02X,%04X,%u",
                 powerboard_state_name(s_status.state), s_status.registers[0],
                 s_status.registers[6], s_status.valid_mask,
                 s_run_deadline_us > now ? 1U : 0U);
#endif
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t effective_gear = cookware_limited_gear(
        s_status.target_gear, s_status.cookware_limited);
    const uint8_t first_gear = retained_resume_first_gear(effective_gear);
    begin_transition_locked(PB_TRANSITION_RESUME,
                            first_gear == 0 ? PB_STATE_ACTIVE_ZERO : PB_STATE_HEATING,
                            first_gear);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    const uint8_t resumed_gear = first_gear;
    const uint8_t resumed_topology = topology_for_gear(first_gear);
#endif
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"active_zero_resume_request\","
                    "\"source\":\"manual_pause\",\"gear\":%u,\"topology\":%u}",
                    esp_timer_get_time() / 1000, resumed_gear, resumed_topology);
    ESP_LOGI(TAG, "Z,REQ,PAUSE,%02X,01,%02X", resumed_topology, resumed_gear);
#endif
    return ESP_OK;
}

esp_err_t powerboard_control_stop(const char *reason)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_status.state == PB_STATE_FAULT) {
        /* Preserve the fault latch so the control task keeps sending Stop. */
        s_force_stop = true;
    } else {
        begin_stop_locked(reason);
    }
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
    return ESP_OK;
}

esp_err_t powerboard_control_lease_begin(uint32_t *generation)
{
#if !MCL02M_COOKING_LEASE_ENABLED
    (void)generation;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (generation == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_status.state != PB_STATE_ARMED || s_status.lease_active) {
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    ++s_status.lease_generation;
    if (s_status.lease_generation == 0) ++s_status.lease_generation;
    s_status.lease_active = true;
    s_status.lease_expired = false;
    s_status.lease_renewals = 0;
    s_lease_deadline_us = esp_timer_get_time() +
        (int64_t)MCL02M_COOKING_LEASE_MS * 1000;
    *generation = s_status.lease_generation;
#if MCL02M_ACTIVE_ZERO_DIAGNOSTICS
    ESP_LOGI(TAG, "L,BEGIN,%" PRIu32 ",%u", *generation,
             MCL02M_COOKING_LEASE_MS);
#endif
    xSemaphoreGive(s_status_lock);
    return ESP_OK;
#endif
}

esp_err_t powerboard_control_lease_renew(uint32_t generation)
{
#if !MCL02M_COOKING_LEASE_ENABLED
    (void)generation;
    return ESP_ERR_NOT_SUPPORTED;
#else
    const int64_t now_us = esp_timer_get_time();
    bool expired = false;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (!s_status.lease_active || generation == 0 ||
        generation != s_status.lease_generation ||
        !state_session_open(s_status.state)) {
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_lease_deadline_us == 0 || now_us >= s_lease_deadline_us) {
        expire_lease_locked();
        expired = true;
    } else {
        s_lease_deadline_us = now_us +
            (int64_t)MCL02M_COOKING_LEASE_MS * 1000;
        ++s_status.lease_renewals;
    }
    xSemaphoreGive(s_status_lock);
    if (expired && s_control_task != NULL) xTaskNotifyGive(s_control_task);
    return expired ? ESP_ERR_TIMEOUT : ESP_OK;
#endif
}

esp_err_t powerboard_control_heartbeat_gap(unsigned duration_ms)
{
    if (duration_ms < 500 || duration_ms > MCL02M_MAX_HEARTBEAT_GAP_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_status.state != PB_STATE_HEATING || s_status.applied_gear > 10) {
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = PB_STATE_HEARTBEAT_GAP;
    s_status.heartbeat_gap_observed_stop = false;
    s_heartbeat_gap_deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    xSemaphoreGive(s_status_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"heartbeat_gap_start\",\"duration_ms\":%u}",
                    esp_timer_get_time() / 1000, duration_ms);
    return ESP_OK;
}

esp_err_t powerboard_control_clear_fault(void)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    if (s_status.state != PB_STATE_FAULT || !s_status.stop_verified ||
        !preflight_healthy_locked()) {
        xSemaphoreGive(s_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = PB_STATE_STOPPED;
    s_status.confirmed_state = PB_STATE_STOPPED;
    s_status.confirmed_gear = 0;
    s_status.confirmation_inferred = false;
    s_status.target_gear = 0;
    s_status.applied_gear = 0;
    s_status.topology = 0;
    s_status.cookware_limited = false;
    reset_i2c_health_locked(false);
    s_start_confirm_deadline_us = 0;
    s_stop_confirm_deadline_us = 0;
    strlcpy(s_status.fault, "NONE", sizeof(s_status.fault));
    s_force_stop = true;
    xSemaphoreGive(s_status_lock);
    if (s_control_task != NULL) xTaskNotifyGive(s_control_task);
    return ESP_OK;
}
