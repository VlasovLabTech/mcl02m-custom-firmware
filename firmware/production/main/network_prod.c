#include "network_prod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "display_prod.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "settings.h"
#include "telemetry.h"

static const char *TAG = "network";
static SemaphoreHandle_t s_lock;
static network_status_t s_status;
static bool s_sta_configured;
static bool s_sntp_started;
static bool s_wifi_started;

static void time_sync_notification(struct timeval *tv)
{
    (void)tv;
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.clock_synchronized = true;
    xSemaphoreGive(s_lock);
}

void network_prod_apply_timezone(void)
{
    app_settings_t settings;
    settings_get(&settings);
    char timezone[24];
    const int minutes = settings.timezone_minutes;
    const char sign = minutes >= 0 ? '-' : '+'; /* POSIX sign is reversed. */
    const int absolute = minutes >= 0 ? minutes : -minutes;
    snprintf(timezone, sizeof(timezone), "UTC%c%d:%02d", sign,
             absolute / 60, absolute % 60);
    setenv("TZ", timezone, 1);
    tzset();
}

static void start_sntp_once(void)
{
    network_prod_apply_timezone();
    if (s_sntp_started) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);
    esp_sntp_init();
    s_sntp_started = true;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.sta_connected = false;
        s_status.sta_ip[0] = 0;
        xSemaphoreGive(s_lock);
        if (s_sta_configured && s_status.enabled) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.sta_connected = true;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_status.sta_ip, sizeof(s_status.sta_ip));
        xSemaphoreGive(s_lock);
        start_sntp_once();
        display_prod_show_wifi_present();
        telemetry_emitf("{\"type\":\"wifi\",\"state\":\"connected\",\"ip\":\"%s\"}",
                        s_status.sta_ip);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.ap_ready = true;
        xSemaphoreGive(s_lock);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.ap_ready = false;
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t connect_ram(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) return err;
    s_sta_configured = true;
    return esp_wifi_connect();
}

esp_err_t network_prod_configure(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool enabled = s_status.enabled;
    xSemaphoreGive(s_lock);
    if (!enabled || !s_wifi_started) return ESP_ERR_INVALID_STATE;
    esp_err_t err = settings_wifi_set(ssid, password);
    if (err != ESP_OK) return err;
    esp_wifi_disconnect();
    return connect_ram(ssid, password);
}

esp_err_t network_prod_set_enabled(bool enabled)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool current = s_status.enabled;
    xSemaphoreGive(s_lock);
    if (enabled == current) return ESP_OK;

    if (!enabled) {
        s_sta_configured = false;
        const esp_err_t err = s_wifi_started ? esp_wifi_stop() : ESP_OK;
        if (err != ESP_OK) return err;
        s_wifi_started = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.enabled = false;
        s_status.sta_connected = false;
        s_status.ap_ready = false;
        s_status.sta_ip[0] = 0;
        xSemaphoreGive(s_lock);
        telemetry_emitf("{\"type\":\"wifi\",\"state\":\"off\"}");
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_started = true;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.enabled = true;
    xSemaphoreGive(s_lock);

    char ssid[33] = {0}, password[64] = {0};
    if (settings_wifi_get(ssid, sizeof(ssid), password, sizeof(password))) {
        err = connect_ram(ssid, password);
        if (err != ESP_OK) ESP_LOGW(TAG, "STA setup failed: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Setup AP %s, web http://192.168.4.1/", s_status.ap_ssid);
    /* The setup AP is useful even when saved STA credentials are stale. */
    return ESP_OK;
}

const char *network_prod_setup_password(void)
{
    return CONFIG_MCL02M_SETUP_AP_PASSWORD;
}

esp_err_t network_prod_init(void)
{
    network_prod_apply_timezone();
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;

    /* The module's BLK0 CRC is invalid. This is RAM-only; no eFuse write. */
    static const uint8_t local_mac[6] = {0x02, 0x4d, 0x43, 0x4c, 0x02, 0x4d};
    esp_err_t err = esp_base_mac_addr_set(local_mac);
    if (err != ESP_OK) return err;
    if ((err = esp_netif_init()) != ESP_OK) return err;
    if ((err = esp_event_loop_create_default()) != ESP_OK) return err;
    if (esp_netif_create_default_wifi_ap() == NULL) return ESP_ERR_NO_MEM;
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    if (sta == NULL) return ESP_ERR_NO_MEM;
    esp_netif_set_hostname(sta, CONFIG_MCL02M_HOSTNAME);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    uint8_t ap_mac[6];
    esp_read_mac(ap_mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "MCL02M-SETUP-%02X%02X",
             ap_mac[4], ap_mac[5]);
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_status.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_status.ap_ssid);
    strlcpy((char *)ap.ap.password, CONFIG_MCL02M_SETUP_AP_PASSWORD,
            sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = strlen(CONFIG_MCL02M_SETUP_AP_PASSWORD) >= 8 ?
                     WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if ((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_AP, &ap)) != ESP_OK) return err;
    app_settings_t settings;
    settings_get(&settings);
    if (settings.wifi_enabled) return network_prod_set_enabled(true);
    ESP_LOGI(TAG, "Wi-Fi is OFF; enable it from the physical Setup menu");
    return ESP_OK;
}

void network_prod_get_status(network_status_t *status)
{
    if (status == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);
}
