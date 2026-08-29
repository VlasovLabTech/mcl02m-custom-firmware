#include "settings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_crc.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

static const char *TAG = "settings";
static const char *NS = "mcl02m_v1";
static app_settings_t s_settings;
_Static_assert(sizeof(app_settings_t) == 32, "settings blob layout changed");
static cooker_profile_t s_profiles[COOKER_PROFILE_COUNT];
static bool s_persistence;
static SemaphoreHandle_t s_lock;

typedef struct {
    uint8_t salt[16];
    uint8_t digest[32];
} admin_record_t;
static admin_record_t s_admin;
static bool s_admin_configured;

typedef struct {
    bool enabled;
    char name[12];
    cook_mode_t mode;
    uint8_t gear;
    uint16_t temperature_c;
    uint32_t timer_s;
} legacy_profile_t;
_Static_assert(sizeof(legacy_profile_t) == 28, "legacy profile layout changed");

unsigned settings_profile_stage_count(const cooker_profile_t *profile)
{
    if (profile == NULL) return 0;
    unsigned count = 0;
    for (unsigned i = 0; i < COOKER_PROFILE_STAGE_COUNT; ++i)
        if (profile->stages[i].timer_s > 0) ++count;
    return count;
}

uint32_t settings_profile_total_s(const cooker_profile_t *profile)
{
    if (profile == NULL) return 0;
    uint32_t total = 0;
    for (unsigned i = 0; i < COOKER_PROFILE_STAGE_COUNT; ++i) {
        if (UINT32_MAX - total < profile->stages[i].timer_s) return UINT32_MAX;
        total += profile->stages[i].timer_s;
    }
    return total;
}

static bool profile_valid(const cooker_profile_t *profile)
{
    if (profile == NULL || memchr(profile->name, 0, sizeof(profile->name)) == NULL ||
        settings_profile_total_s(profile) > COOKER_MAX_TIMER_S)
        return false;
    for (const unsigned char *at = (const unsigned char *)profile->name; *at; ++at)
        if (*at < 0x20 || *at == '"' || *at == '\\') return false;
    for (unsigned i = 0; i < COOKER_PROFILE_STAGE_COUNT; ++i) {
        const cooker_profile_stage_t *stage = &profile->stages[i];
        if (stage->mode > COOK_MODE_TEMPERATURE || stage->gear > COOKER_MAX_GEAR ||
            stage->temperature_c < COOKER_TEMP_MIN_C ||
            stage->temperature_c > COOKER_TEMP_MAX_C ||
            stage->timer_s > COOKER_MAX_TIMER_S)
            return false;
    }
    return true;
}

static bool oled_timeout_valid(uint16_t seconds)
{
    static const uint16_t allowed[] = {
        60, 120, 180, 300, 600, 1200, 1800, 3600, 7200, 10800, 14400, 18000,
    };
    for (unsigned i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (seconds == allowed[i]) return true;
    return false;
}

static uint32_t settings_crc(const app_settings_t *value)
{
    return esp_crc32_le(0, (const uint8_t *)value,
                        offsetof(app_settings_t, crc32));
}

static void load_defaults(void)
{
    memset(&s_admin, 0, sizeof(s_admin));
    s_admin_configured = false;
    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.schema = COOKER_SETTINGS_SCHEMA;
    s_settings.language = LANG_EN;
    s_settings.sound_enabled = 1;
    s_settings.show_context_value = 1;
    s_settings.show_igbt = 0;
    s_settings.timer_screen_mode = TIMER_SCREEN_AUTO;
    s_settings.sleep_minutes = COOKER_DEFAULT_SLEEP_MIN;
    s_settings.oled_timeout_s = COOKER_DEFAULT_OLED_TIMEOUT_S;
    s_settings.timezone_minutes = 180;
    s_settings.show_sleep_clock = 1;
    s_settings.wifi_enabled = 0;
    s_settings.show_i2c_debug = 0;
    s_settings.crc32 = settings_crc(&s_settings);

    memset(s_profiles, 0, sizeof(s_profiles));
    for (unsigned i = 0; i < COOKER_PROFILE_COUNT; ++i) {
        snprintf(s_profiles[i].name, sizeof(s_profiles[i].name), "PROFILE %u", i + 1);
        for (unsigned stage = 0; stage < COOKER_PROFILE_STAGE_COUNT; ++stage) {
            s_profiles[i].stages[stage].mode = COOK_MODE_POWER;
            s_profiles[i].stages[stage].gear = 30;
            s_profiles[i].stages[stage].temperature_c = 100;
        }
    }
}

static esp_err_t open_rw(nvs_handle_t *handle)
{
    return s_persistence ? nvs_open(NS, NVS_READWRITE, handle) : ESP_ERR_INVALID_STATE;
}

esp_err_t settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    load_defaults();
    if (psa_crypto_init() != PSA_SUCCESS) {
        ESP_LOGE(TAG, "PSA crypto init failed");
        return ESP_FAIL;
    }
    /* Never erase NVS automatically: stock rollback data must remain recoverable. */
    const esp_err_t init = nvs_flash_init();
    if (init != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable (%s); using RAM defaults without erase",
                 esp_err_to_name(init));
        return ESP_OK;
    }
    s_persistence = true;

    nvs_handle_t handle = 0;
    if (nvs_open(NS, NVS_READONLY, &handle) != ESP_OK) return ESP_OK;
    app_settings_t stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, "settings", &stored, &size) == ESP_OK &&
        size == sizeof(stored) &&
        (stored.schema == 1U || stored.schema == 2U || stored.schema == 3U ||
         stored.schema == 4U ||
         stored.schema == COOKER_SETTINGS_SCHEMA) &&
        stored.crc32 == settings_crc(&stored)) {
        s_settings = stored;
        if (stored.schema != COOKER_SETTINGS_SCHEMA) {
            /* RAM-only migration; persist only on explicit Save. */
            s_settings.schema = COOKER_SETTINGS_SCHEMA;
            if (stored.schema == 1U) {
                s_settings.show_context_value = 1;
                s_settings.show_igbt = 0;
                s_settings.oled_timeout_s = COOKER_DEFAULT_OLED_TIMEOUT_S;
            }
            if (stored.schema <= 2U) s_settings.show_sleep_clock = 1;
            if (stored.schema <= 4U) s_settings.show_i2c_debug = 0;
            /* Wi-Fi is intentionally opt-in after this upgrade. */
            s_settings.wifi_enabled = 0;
            s_settings.crc32 = settings_crc(&s_settings);
        }
    }
#if !COOKER_I2C_DEBUG_DISPLAY_ENABLED
    /* RAM-only release override; the stored field and implementation stay reusable. */
    s_settings.show_i2c_debug = 0;
    s_settings.crc32 = settings_crc(&s_settings);
#endif
    size = 0;
    if (nvs_get_blob(handle, "profiles", NULL, &size) == ESP_OK) {
        if (size == sizeof(s_profiles)) {
            cooker_profile_t stored_profiles[COOKER_PROFILE_COUNT];
            if (nvs_get_blob(handle, "profiles", stored_profiles, &size) == ESP_OK) {
                for (unsigned i = 0; i < COOKER_PROFILE_COUNT; ++i)
                    if (profile_valid(&stored_profiles[i])) s_profiles[i] = stored_profiles[i];
            }
        } else if (size == sizeof(legacy_profile_t) * COOKER_PROFILE_COUNT) {
            legacy_profile_t legacy[COOKER_PROFILE_COUNT];
            if (nvs_get_blob(handle, "profiles", legacy, &size) == ESP_OK) {
                for (unsigned i = 0; i < COOKER_PROFILE_COUNT; ++i) {
                    memcpy(s_profiles[i].name, legacy[i].name, sizeof(s_profiles[i].name));
                    s_profiles[i].name[sizeof(s_profiles[i].name) - 1] = '\0';
                    if (legacy[i].enabled && legacy[i].timer_s > 0) {
                        s_profiles[i].stages[0].mode = legacy[i].mode;
                        s_profiles[i].stages[0].gear = legacy[i].gear;
                        s_profiles[i].stages[0].temperature_c = legacy[i].temperature_c;
                        s_profiles[i].stages[0].timer_s = legacy[i].timer_s;
                    }
                }
            }
        }
    }
    size = sizeof(s_admin);
    s_admin_configured = nvs_get_blob(handle, "admin", &s_admin, &size) == ESP_OK &&
                         size == sizeof(s_admin);
    nvs_close(handle);
    return ESP_OK;
}

void settings_get(app_settings_t *settings)
{
    if (settings == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *settings = s_settings;
    xSemaphoreGive(s_lock);
}

esp_err_t settings_update(const app_settings_t *settings)
{
    if (settings == NULL || settings->language > LANG_ZH ||
        settings->timer_screen_mode > TIMER_SCREEN_ALWAYS ||
        settings->show_i2c_debug > 1 ||
        settings->sleep_minutes < 1 || settings->sleep_minutes > 60 ||
        !oled_timeout_valid(settings->oled_timeout_s) ||
        settings->timezone_minutes < -720 || settings->timezone_minutes > 840)
        return ESP_ERR_INVALID_ARG;

    app_settings_t next = *settings;
    next.schema = COOKER_SETTINGS_SCHEMA;
    next.crc32 = settings_crc(&next);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (memcmp(&next, &s_settings, sizeof(next)) == 0) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    s_settings = next;
    if (!s_persistence) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = open_rw(&handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, "settings", &s_settings, sizeof(s_settings));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    xSemaphoreGive(s_lock);
    return err;
}

bool settings_persistence_available(void)
{
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool available = s_persistence;
    xSemaphoreGive(s_lock);
    return available;
}

bool settings_wifi_get(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    if (ssid == NULL || password == NULL || s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_persistence) {
        xSemaphoreGive(s_lock);
        return false;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(NS, NVS_READONLY, &handle) != ESP_OK) {
        xSemaphoreGive(s_lock);
        return false;
    }
    size_t a = ssid_size, b = password_size;
    const bool ok = nvs_get_str(handle, "wifi_ssid", ssid, &a) == ESP_OK &&
                    nvs_get_str(handle, "wifi_pass", password, &b) == ESP_OK && ssid[0];
    nvs_close(handle);
    xSemaphoreGive(s_lock);
    return ok;
}

esp_err_t settings_wifi_set(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL || !ssid[0] || strlen(ssid) > 32 ||
        strlen(password) > 63 || s_lock == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t handle = 0;
    esp_err_t err = open_rw(&handle);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_pass", password);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t settings_wifi_clear(void)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t handle = 0;
    esp_err_t err = open_rw(&handle);
    if (err == ESP_OK) {
        nvs_erase_key(handle, "wifi_ssid");
        nvs_erase_key(handle, "wifi_pass");
        err = nvs_commit(handle);
        nvs_close(handle);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t settings_factory_reset(void)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t handle = 0;
    esp_err_t err = open_rw(&handle);
    if (err == ESP_OK) err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    if (err == ESP_OK) load_defaults();
    xSemaphoreGive(s_lock);
    return err;
}

static void admin_hash(const uint8_t salt[16], const char *password, uint8_t output[32])
{
    uint8_t input[16 + 63];
    const size_t password_length = strlen(password);
    memcpy(input, salt, 16);
    memcpy(input + 16, password, password_length);
    size_t output_length = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, input, 16 + password_length,
                         output, 32, &output_length) != PSA_SUCCESS || output_length != 32) {
        memset(output, 0, 32);
        return;
    }
    /* Deliberately slow a password guess without storing plaintext. */
    uint8_t round[48];
    for (unsigned iteration = 1; iteration < 20000; ++iteration) {
        memcpy(round, output, 32);
        memcpy(round + 32, salt, 16);
        if (psa_hash_compute(PSA_ALG_SHA_256, round, sizeof(round), output, 32,
                             &output_length) != PSA_SUCCESS || output_length != 32) {
            memset(output, 0, 32);
            return;
        }
    }
}

static bool admin_load(admin_record_t *record)
{
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_admin_configured;
    if (ok) *record = s_admin;
    xSemaphoreGive(s_lock);
    return ok;
}

bool settings_admin_is_configured(void)
{
    admin_record_t record;
    return admin_load(&record);
}

bool settings_admin_verify(const char *password)
{
    admin_record_t record;
    uint8_t digest[32];
    if (password == NULL || !admin_load(&record)) return false;
    admin_hash(record.salt, password, digest);
    uint8_t difference = 0;
    for (unsigned i = 0; i < sizeof(digest); ++i) difference |= digest[i] ^ record.digest[i];
    return difference == 0;
}

esp_err_t settings_admin_set(const char *password)
{
    if (password == NULL || strlen(password) < 8 || strlen(password) > 63)
        return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    admin_record_t record;
    esp_fill_random(record.salt, sizeof(record.salt));
    admin_hash(record.salt, password, record.digest);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t handle = 0;
    esp_err_t err = open_rw(&handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, "admin", &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        s_admin = record;
        s_admin_configured = true;
    }
    if (handle != 0) nvs_close(handle);
    xSemaphoreGive(s_lock);
    return err;
}

void settings_profiles_get(cooker_profile_t profiles[COOKER_PROFILE_COUNT])
{
    if (profiles == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(profiles, s_profiles, sizeof(s_profiles));
    xSemaphoreGive(s_lock);
}

esp_err_t settings_profile_set(unsigned index, const cooker_profile_t *profile)
{
    if (index >= COOKER_PROFILE_COUNT || profile == NULL || !profile_valid(profile))
        return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (memcmp(&s_profiles[index], profile, sizeof(*profile)) == 0) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    s_profiles[index] = *profile;
    if (!s_persistence) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = open_rw(&handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, "profiles", s_profiles, sizeof(s_profiles));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    xSemaphoreGive(s_lock);
    return err;
}
