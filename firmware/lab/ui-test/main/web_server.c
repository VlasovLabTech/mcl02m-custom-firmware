#include "web_server.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "network.h"
#include "powerboard_ro.h"
#include "telemetry.h"
#include "ui_outputs.h"

static const char *TAG = "web";
static httpd_handle_t s_server;
static char s_token[17];

static const char PAGE[] =
"<!doctype html><html lang='ru'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>MCL02M UI Test</title><style>"
"body{font-family:system-ui;margin:20px;max-width:960px;background:#101215;color:#eee}"
"fieldset{margin:12px 0;padding:12px;border:1px solid #555}button,input,select{margin:4px;padding:8px}"
"button{cursor:pointer}.danger{background:#b22;color:white;font-weight:700}"
"pre{height:300px;overflow:auto;background:#050606;padding:10px;white-space:pre-wrap}"
".note{color:#ffd26a}.ok{color:#75e69a}</style></head><body>"
"<h1>MCL02M — безопасный UI test</h1>"
"<p class='note'>Кнопки и энкодер только логируются. Управления нагревом в этой прошивке нет.</p>"
"<label>Токен из UART: <input id='token' autocomplete='off'></label>"
"<button class='danger' onclick=cmd('/api/alloff')>ALL OFF</button>"
"<fieldset><legend>Входы</legend>"
"<p>Покрутите энкодер в обе стороны; нажмите центр коротко и подержите; нажмите левый, правый и оба сенсора. Смотрите журнал.</p>"
"</fieldset>"
"<fieldset><legend>OLED 64×48</legend>"
"<button onclick=cmd('/api/oled?pattern=1')>Шахматка 8×8</button>"
"<button onclick=cmd('/api/oled?pattern=5')>Шахматка 4×4</button>"
"<button onclick=cmd('/api/oled?pattern=2')>Угловые фигуры</button>"
"<button onclick=cmd('/api/oled?pattern=3')>Сетка 8 px</button>"
"<button onclick=cmd('/api/oled?pattern=6')>Круги</button>"
"<button onclick=cmd('/api/oled?pattern=7')>Диагонали</button>"
"<button onclick=cmd('/api/oled?pattern=4')>Все пиксели</button>"
"<button onclick=cmd('/api/oled?pattern=0')>Очистить</button></fieldset>"
"<fieldset><legend>Девять LED мощности</legend>"
"<input id='powerLevel' type='number' min='0' max='9' value='1'>"
"<button onclick=power()>Показать уровень на 2 с</button></fieldset>"
"<fieldset><legend>Raw serial LED mapping</legend>"
"<label>Byte <select id='ledIndex'><option>0</option><option>1</option><option>2</option></select></label>"
"<label>Value 0x<input id='ledValue' value='01' size='4'></label>"
"<button onclick=rawLed()>Импульс 1 с</button>"
"<p>Для blue/orange начинаем с byte 2: value 02, затем 04.</p></fieldset>"
"<fieldset><legend>GPIO22 / GPIO32</legend>"
"<button onclick=cmd('/api/direct?gpio=22&level=1&ms=500')>GPIO22 high 0.5 с</button>"
"<button onclick=cmd('/api/direct?gpio=32&level=1&ms=500')>GPIO32 high 0.5 с</button></fieldset>"
"<fieldset><legend>Пищалка</legend>"
"<button onclick=cmd('/api/buzzer?hz=2000&ms=100')>2 kHz / 100 ms</button>"
"<button onclick=cmd('/api/buzzer?hz=4000&ms=100')>4 kHz / 100 ms</button></fieldset>"
"<fieldset><legend>Силовая плата — только чтение</legend>"
"<button onclick=selfcheck()>Read R20…R27 / self-check</button>"
"<div id='selfcheck'></div></fieldset>"
"<fieldset><legend>Домашний Wi-Fi (RAM only)</legend>"
"<input id='ssid' placeholder='SSID'><input id='password' type='password' placeholder='Password'>"
"<button onclick=wifi()>Подключить STA</button>"
"<p>После подключения вернитесь в обычную Wi-Fi сеть; новый IP появится в UART и журнале.</p></fieldset>"
"<h2>Live telemetry</h2><pre id='log'></pre>"
"<script>"
"const log=document.getElementById('log');"
"function add(x){log.textContent+=x+'\\n';log.scrollTop=log.scrollHeight}"
"function hdr(){return {'X-Test-Token':document.getElementById('token').value}}"
"async function cmd(u){try{let r=await fetch(u,{method:'POST',headers:hdr()});add(await r.text())}catch(e){add(e)}}"
"function power(){cmd('/api/power-led?level='+powerLevel.value+'&ms=2000')}"
"function rawLed(){cmd('/api/led?index='+ledIndex.value+'&value='+parseInt(ledValue.value,16)+'&ms=1000')}"
"async function selfcheck(){let r=await fetch('/api/selfcheck');let t=await r.text();document.getElementById('selfcheck').textContent=t;add(t)}"
"async function wifi(){let b=new URLSearchParams({ssid:ssid.value,password:password.value});let r=await fetch('/api/wifi',{method:'POST',headers:{...hdr(),'Content-Type':'application/x-www-form-urlencoded'},body:b});add(await r.text())}"
"let ws;function connect(){ws=new WebSocket('ws://'+location.host+'/ws');ws.onmessage=e=>add(e.data);ws.onclose=()=>setTimeout(connect,1000)}connect();"
"</script></body></html>";

typedef struct {
    httpd_handle_t server;
    int fd;
    char payload[TELEMETRY_MESSAGE_MAX];
} ws_job_t;

static void ws_send_job(void *arg)
{
    ws_job_t *job = arg;
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)job->payload,
        .len = strlen(job->payload),
    };
    httpd_ws_send_frame_async(job->server, job->fd, &frame);
    free(job);
}

static void telemetry_web_sink(const char *json, void *ctx)
{
    httpd_handle_t server = ctx;
    if (server == NULL) return;

    int clients[8];
    size_t count = sizeof(clients) / sizeof(clients[0]);
    if (httpd_get_client_list(server, &count, clients) != ESP_OK) return;
    for (size_t i = 0; i < count; ++i) {
        if (httpd_ws_get_fd_info(server, clients[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        ws_job_t *job = calloc(1, sizeof(*job));
        if (job == NULL) continue;
        job->server = server;
        job->fd = clients[i];
        strlcpy(job->payload, json, sizeof(job->payload));
        if (httpd_queue_work(server, ws_send_job, job) != ESP_OK) {
            free(job);
        }
    }
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_result(httpd_req_t *req, esp_err_t err)
{
    char response[96];
    snprintf(response, sizeof(response), "{\"ok\":%s,\"esp_err\":%d,\"name\":\"%s\"}",
             err == ESP_OK ? "true" : "false", err, esp_err_to_name(err));
    if (err != ESP_OK) httpd_resp_set_status(req, "400 Bad Request");
    return send_json(req, response);
}

static bool token_valid(httpd_req_t *req)
{
    char token[32];
    const size_t length = httpd_req_get_hdr_value_len(req, "X-Test-Token");
    if (length == 0 || length >= sizeof(token)) return false;
    if (httpd_req_get_hdr_value_str(req, "X-Test-Token", token, sizeof(token)) != ESP_OK) return false;
    return strcmp(token, s_token) == 0;
}

static esp_err_t require_token(httpd_req_t *req)
{
    if (token_valid(req)) return ESP_OK;
    httpd_resp_set_status(req, "403 Forbidden");
    send_json(req, "{\"ok\":false,\"error\":\"UART token required\"}");
    return ESP_ERR_INVALID_STATE;
}

static bool query_uint(httpd_req_t *req, const char *key, unsigned *value)
{
    char query[128];
    char text[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK) return false;
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed > UINT32_MAX) return false;
    *value = (unsigned)parsed;
    return true;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK;
    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    return httpd_ws_recv_frame(req, &frame, 0);
}

static esp_err_t alloff_handler(httpd_req_t *req)
{
    ui_outputs_all_off("remote_all_off");
    return send_result(req, ESP_OK);
}

static esp_err_t led_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned index, value, ms;
    if (!query_uint(req, "index", &index) || !query_uint(req, "value", &value) ||
        !query_uint(req, "ms", &ms) || value > 255) return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, ui_led_raw_pulse(index, (uint8_t)value, ms));
}

static esp_err_t power_led_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned level, ms;
    if (!query_uint(req, "level", &level) || !query_uint(req, "ms", &ms))
        return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, ui_led_power_level(level, ms));
}

static esp_err_t direct_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned gpio, level, ms;
    if (!query_uint(req, "gpio", &gpio) || !query_uint(req, "level", &level) ||
        !query_uint(req, "ms", &ms)) return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, ui_direct_output_pulse(gpio, level, ms));
}

static esp_err_t oled_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned pattern;
    if (!query_uint(req, "pattern", &pattern)) return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, ui_oled_show_pattern(pattern));
}

static esp_err_t buzzer_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned hz, ms;
    if (!query_uint(req, "hz", &hz) || !query_uint(req, "ms", &ms))
        return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, ui_buzzer_chirp(hz, ms));
}

static esp_err_t selfcheck_handler(httpd_req_t *req)
{
    char response[2048] = {0};
    const esp_err_t err = powerboard_ro_snapshot_json(response, sizeof(response));
    if (err != ESP_OK) httpd_resp_set_status(req, "503 Service Unavailable");
    return send_json(req, response);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *text)
{
    char *source = text;
    char *dest = text;
    while (*source) {
        if (*source == '+') {
            *dest++ = ' ';
            source++;
        } else if (*source == '%' && source[1] != '\0' && source[2] != '\0' &&
                   hex_value(source[1]) >= 0 && hex_value(source[2]) >= 0) {
            *dest++ = (char)((hex_value(source[1]) << 4) | hex_value(source[2]));
            source += 3;
        } else {
            *dest++ = *source++;
        }
    }
    *dest = '\0';
}

static esp_err_t wifi_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    if (req->content_len <= 0 || req->content_len >= 180) return send_result(req, ESP_ERR_INVALID_SIZE);
    char body[180] = {0};
    const int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) return send_result(req, ESP_FAIL);
    body[received] = '\0';

    char ssid[33] = {0};
    char password[64] = {0};
    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        httpd_query_key_value(body, "password", password, sizeof(password)) != ESP_OK) {
        return send_result(req, ESP_ERR_INVALID_ARG);
    }
    url_decode(ssid);
    url_decode(password);
    return send_result(req, network_connect_sta_ram(ssid, password));
}

esp_err_t web_server_start(void)
{
    const uint32_t random0 = esp_random();
    const uint32_t random1 = esp_random();
    snprintf(s_token, sizeof(s_token), "%08" PRIx32 "%08" PRIx32, random0, random1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true},
        {.uri = "/api/alloff", .method = HTTP_POST, .handler = alloff_handler},
        {.uri = "/api/led", .method = HTTP_POST, .handler = led_handler},
        {.uri = "/api/power-led", .method = HTTP_POST, .handler = power_led_handler},
        {.uri = "/api/direct", .method = HTTP_POST, .handler = direct_handler},
        {.uri = "/api/oled", .method = HTTP_POST, .handler = oled_handler},
        {.uri = "/api/buzzer", .method = HTTP_POST, .handler = buzzer_handler},
        {.uri = "/api/selfcheck", .method = HTTP_GET, .handler = selfcheck_handler},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &handlers[i]);
        if (err != ESP_OK) {
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }

    telemetry_set_sink(telemetry_web_sink, s_server);
    ESP_LOGW(TAG, "REMOTE OUTPUT TOKEN: %s", s_token);
    ESP_LOGI(TAG, "Fallback page: http://192.168.4.1/ (AP %s)", network_ap_ssid());
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"web_ready\","
                    "\"ap_url\":\"http://192.168.4.1/\"}", esp_timer_get_time() / 1000);
    return ESP_OK;
}

const char *web_server_token(void)
{
    return s_token;
}
