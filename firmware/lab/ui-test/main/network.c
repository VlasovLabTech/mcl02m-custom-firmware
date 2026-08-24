#include "network.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "telemetry.h"

static const char *TAG = "network";
static char s_ap_ssid[33];
static bool s_sta_configured;
static esp_netif_t *s_sta_netif;

#ifndef MCL02M_STA_SSID_HEX
#define MCL02M_STA_SSID_HEX ""
#define MCL02M_STA_PASSWORD_HEX ""
#endif

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_hex_string(const char *hex, char *output, size_t output_size)
{
    const size_t hex_length = strlen(hex);
    if ((hex_length & 1) != 0 || hex_length / 2 >= output_size) {
        return false;
    }
    for (size_t i = 0; i < hex_length / 2; ++i) {
        const int high = hex_nibble(hex[i * 2]);
        const int low = hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[i] = (char)((high << 4) | low);
    }
    output[hex_length / 2] = '\0';
    return true;
}

static void network_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"wifi_sta\","
                        "\"state\":\"disconnected\",\"reason\":%u}",
                        esp_timer_get_time() / 1000, event == NULL ? 0 : event->reason);
        if (s_sta_configured) {
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "Home-network address: http://%s/", ip);
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"wifi_sta\","
                        "\"state\":\"connected\",\"ip\":\"%s\"}",
                        esp_timer_get_time() / 1000, ip);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Fallback AP: %s, page: http://192.168.4.1/", s_ap_ssid);
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"wifi_ap\","
                        "\"state\":\"ready\",\"ssid\":\"%s\","
                        "\"ip\":\"192.168.4.1\"}",
                        esp_timer_get_time() / 1000, s_ap_ssid);
    }
}

esp_err_t network_connect_sta_ram(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL || ssid[0] == '\0' ||
        strlen(ssid) > 32 || strlen(password) > 63) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.threshold.authmode = password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return err;
    }
    s_sta_configured = true;
    err = esp_wifi_connect();
    if (err == ESP_OK) {
        telemetry_emitf("{\"t_ms\":%lld,\"type\":\"wifi_sta\","
                        "\"state\":\"connecting\",\"ssid\":\"%s\","
                        "\"storage\":\"RAM_ONLY\"}", esp_timer_get_time() / 1000, ssid);
    }
    return err;
}

esp_err_t network_init(void)
{
    /*
     * This Xiaomi module has a usable factory MAC payload but a bad BLK0 CRC,
     * so current ESP-IDF correctly rejects it.  Use a locally administered,
     * unicast address for this isolated test firmware.  This changes RAM-only
     * network identity; no eFuse or flash data is written.
     */
    static const uint8_t test_base_mac[6] = {0x02, 0x4d, 0x43, 0x4c, 0x02, 0x4d};
    esp_err_t err = esp_base_mac_addr_set(test_base_mac);
    if (err != ESP_OK) return err;
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"wifi_base_mac\","
                    "\"source\":\"RAM_LOCAL_ADMIN\","
                    "\"mac\":\"02:4D:43:4C:02:4D\"}",
                    esp_timer_get_time() / 1000);

    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;

    if (esp_netif_create_default_wifi_ap() == NULL) return ESP_ERR_NO_MEM;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) return ESP_ERR_NO_MEM;
    err = esp_netif_set_hostname(s_sta_netif, "mcl02m-test");
    if (err != ESP_OK) return err;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "MCL02M-TEST-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    strlcpy((char *)ap.ap.password, CONFIG_MCL02M_TEST_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = strlen(CONFIG_MCL02M_TEST_AP_PASSWORD) >= 8 ?
                     WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    char embedded_ssid[33] = {0};
    char embedded_password[64] = {0};
    const bool embedded_ok = decode_hex_string(MCL02M_STA_SSID_HEX,
                                                embedded_ssid, sizeof(embedded_ssid)) &&
                             decode_hex_string(MCL02M_STA_PASSWORD_HEX,
                                                embedded_password, sizeof(embedded_password));
    const char *sta_ssid = embedded_ok && embedded_ssid[0] != '\0' ?
                           embedded_ssid : CONFIG_MCL02M_STA_SSID;
    const char *sta_password = embedded_ok && embedded_ssid[0] != '\0' ?
                               embedded_password : CONFIG_MCL02M_STA_PASSWORD;
    if (sta_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Automatic RAM-only STA connection configured for SSID: %s", sta_ssid);
        err = network_connect_sta_ram(sta_ssid, sta_password);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Build-time STA connect setup failed: %s", esp_err_to_name(err));
        }
    }
    return ESP_OK;
}

const char *network_ap_ssid(void)
{
    return s_ap_ssid;
}
