#include "web_server_power.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "network.h"
#include "powerboard_control.h"
#include "telemetry.h"
#include "ui_outputs.h"

static const char *TAG = "power_web";
static httpd_handle_t s_server;
static char s_token[17];

static const char PAGE[] =
"<!doctype html><html lang='ru'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>MCL02M Power Bring-up</title><style>"
"body{font-family:system-ui;margin:20px;max-width:1000px;background:#101215;color:#eee}"
"fieldset{margin:12px 0;padding:12px;border:1px solid #555}button,input{margin:4px;padding:9px}"
"button{cursor:pointer}.stop{background:#c22;color:#fff;font-weight:800}.arm{background:#d98200;color:#fff}"
".run{background:#187d37;color:#fff}.warn{color:#ffd26a}.state{font-size:18px;background:#050606;padding:12px}"
"pre{height:300px;overflow:auto;background:#050606;padding:10px;white-space:pre-wrap}</style></head><body>"
"<h1>MCL02M — силовой bring-up</h1>"
"<p class='warn'>Нагрев возможен только после ARM. Все команды ограничены локальным таймером и температурными пределами ESP32.</p>"
"<label>UART token: <input id='token' autocomplete='off'></label>"
"<button class='stop' onclick=post('/api/power/stop')>STOP NOW</button>"
"<div id='status' class='state'>loading...</div>"
"<fieldset><legend>Разрешение и запуск</legend>"
"<button class='arm' onclick=post('/api/power/arm?ms=30000')>ARM на 30 с</button>"
"<label>Gear <input id='gear' type='number' min='1' max='99' value='10'></label>"
"<label>Duration ms <input id='duration' type='number' min='1000' max='300000' value='5000'></label>"
"<button class='run' onclick=startPower()>START</button>"
"<button onclick=setGear()>SET GEAR</button>"
"</fieldset>"
"<fieldset><legend>Состояния</legend>"
"<button onclick=post('/api/power/pause')>PAUSE</button>"
"<button onclick=post('/api/power/resume')>RESUME</button>"
"<button onclick=post('/api/power/clear-fault')>CLEAR FAULT</button>"
"</fieldset>"
"<fieldset><legend>Failsafe — только gear ≤10</legend>"
"<label>Gap ms <input id='gap' type='number' min='500' max='5000' value='3000'></label>"
"<button class='arm' onclick=heartbeatGap()>SKIP HEARTBEAT</button>"
"<p>Во время gap чтения продолжаются, но W0D/W00/W0C не передаются. После gap ESP32 обязательно отправляет Stop и фиксирует Fault.</p>"
"</fieldset>"
"<fieldset><legend>Wi-Fi RAM only</legend>"
"<input id='ssid' placeholder='SSID'><input id='password' type='password' placeholder='Password'>"
"<button onclick=wifi()>CONNECT</button></fieldset>"
"<h2>Live telemetry</h2><pre id='log'></pre>"
"<script>"
"const log=document.getElementById('log'),statusBox=document.getElementById('status');"
"const token=document.getElementById('token');token.value=localStorage.getItem('mclPowerToken')||'';"
"token.onchange=()=>localStorage.setItem('mclPowerToken',token.value);"
"function add(x){log.textContent+=x+'\\n';log.scrollTop=log.scrollHeight}"
"function hdr(){return {'X-Test-Token':token.value}}"
"async function post(u){try{let r=await fetch(u,{method:'POST',headers:hdr()});let t=await r.text();add(t);await refresh()}catch(e){add(e)}}"
"function startPower(){post('/api/power/start?gear='+gear.value+'&ms='+duration.value)}"
"function setGear(){post('/api/power/gear?gear='+gear.value)}"
"function heartbeatGap(){if(confirm('Кастрюля с водой установлена, gear не выше 10, оператор рядом?'))post('/api/power/hb-gap?confirm=1&ms='+gap.value)}"
"async function refresh(){try{let r=await fetch('/api/power/status');statusBox.textContent=JSON.stringify(await r.json(),null,2)}catch(e){statusBox.textContent=e}}"
"async function wifi(){let b=new URLSearchParams({ssid:ssid.value,password:password.value});let r=await fetch('/api/wifi',{method:'POST',headers:{...hdr(),'Content-Type':'application/x-www-form-urlencoded'},body:b});add(await r.text())}"
"let ws;function connect(){ws=new WebSocket('ws://'+location.host+'/ws');ws.onmessage=e=>add(e.data);ws.onclose=()=>setTimeout(connect,1000)}connect();"
"setInterval(refresh,500);refresh();"
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
        if (httpd_queue_work(server, ws_send_job, job) != ESP_OK) free(job);
    }
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_result(httpd_req_t *req, esp_err_t err)
{
    char response[112];
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
    const unsigned long parsed = strtoul(text, &end, 0);
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

static esp_err_t status_handler(httpd_req_t *req)
{
    char response[768];
    powerboard_control_status_json(response, sizeof(response));
    return send_json(req, response);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    return send_result(req, powerboard_control_stop("REMOTE STOP"));
}

static esp_err_t alloff_handler(httpd_req_t *req)
{
    powerboard_control_stop("ALL OFF");
    ui_outputs_all_off("remote_all_off");
    return send_result(req, ESP_OK);
}

static esp_err_t arm_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned ms;
    if (!query_uint(req, "ms", &ms)) return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, powerboard_control_arm(ms));
}

static esp_err_t start_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned gear, ms;
    if (!query_uint(req, "gear", &gear) || !query_uint(req, "ms", &ms))
        return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, powerboard_control_start(gear, ms));
}

static esp_err_t gear_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned gear;
    if (!query_uint(req, "gear", &gear)) return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, powerboard_control_set_gear(gear));
}

static esp_err_t pause_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    return send_result(req, powerboard_control_pause());
}

static esp_err_t resume_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    return send_result(req, powerboard_control_resume());
}

static esp_err_t clear_fault_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    return send_result(req, powerboard_control_clear_fault());
}

static esp_err_t heartbeat_gap_handler(httpd_req_t *req)
{
    if (require_token(req) != ESP_OK) return ESP_OK;
    unsigned confirm, ms;
    if (!query_uint(req, "confirm", &confirm) || confirm != 1 ||
        !query_uint(req, "ms", &ms)) return send_result(req, ESP_ERR_INVALID_ARG);
    return send_result(req, powerboard_control_heartbeat_gap(ms));
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
            ++source;
        } else if (*source == '%' && source[1] && source[2] &&
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
        httpd_query_key_value(body, "password", password, sizeof(password)) != ESP_OK)
        return send_result(req, ESP_ERR_INVALID_ARG);
    url_decode(ssid);
    url_decode(password);
    return send_result(req, network_connect_sta_ram(ssid, password));
}

esp_err_t web_server_power_start(void)
{
    snprintf(s_token, sizeof(s_token), "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.stack_size = 7168;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t handlers[] = {
        {.uri="/", .method=HTTP_GET, .handler=root_handler},
        {.uri="/ws", .method=HTTP_GET, .handler=ws_handler, .is_websocket=true},
        {.uri="/api/power/status", .method=HTTP_GET, .handler=status_handler},
        {.uri="/api/power/stop", .method=HTTP_POST, .handler=stop_handler},
        {.uri="/api/alloff", .method=HTTP_POST, .handler=alloff_handler},
        {.uri="/api/power/arm", .method=HTTP_POST, .handler=arm_handler},
        {.uri="/api/power/start", .method=HTTP_POST, .handler=start_handler},
        {.uri="/api/power/gear", .method=HTTP_POST, .handler=gear_handler},
        {.uri="/api/power/pause", .method=HTTP_POST, .handler=pause_handler},
        {.uri="/api/power/resume", .method=HTTP_POST, .handler=resume_handler},
        {.uri="/api/power/clear-fault", .method=HTTP_POST, .handler=clear_fault_handler},
        {.uri="/api/power/hb-gap", .method=HTTP_POST, .handler=heartbeat_gap_handler},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_handler},
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
    ESP_LOGW(TAG, "POWER CONTROL TOKEN: %s", s_token);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"power_web_ready\","
                    "\"ap_url\":\"http://192.168.4.1/\"}", esp_timer_get_time() / 1000);
    return ESP_OK;
}

const char *web_server_power_token(void)
{
    return s_token;
}
