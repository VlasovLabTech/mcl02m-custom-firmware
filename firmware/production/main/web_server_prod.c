#include "web_server_prod.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_types.h"
#include "cooking_engine.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "network_prod.h"
#include "powerboard_control.h"
#include "settings.h"
#include "sound.h"
#include "telemetry.h"

#define BODY_MAX 768

static httpd_handle_t s_server;
static char s_session[33];
static char s_csrf[33];
static int64_t s_session_deadline_us;
static int64_t s_login_blocked_until_us;
static unsigned s_login_failures;

static const char PAGE[] =
"<!doctype html><html lang=ru><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>MCL02M</title><style>"
":root{color-scheme:dark}body{font:16px system-ui;margin:auto;max-width:920px;padding:18px;background:#101214;color:#eef}"
"section{background:#1b1f23;border-radius:12px;padding:14px;margin:12px 0}button,input,select{font:inherit;padding:9px;margin:4px;border-radius:8px;border:1px solid #667;background:#292f35;color:#fff}"
"button.stop{background:#a22}button.start{background:#185f35}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}"
"pre{white-space:pre-wrap}.muted{color:#9aa}#settings label{display:block;margin:7px 0}.stage{border:1px solid #454b52;border-radius:9px;padding:8px;margin:8px 0}.stage label{display:block}</style>"
"<h1>MCL02M Custom</h1><p id=net class=muted>Connecting…</p>"
"<section id=auth><h2>Вход / первичная настройка</h2><input id=pw type=password placeholder='Пароль (мин. 8)'>"
"<button onclick=login()>Войти</button><button onclick=setup()>Задать впервые</button></section>"
"<main id=app hidden><section><h2>Состояние и диагностика</h2><p class=muted>Нагрев управляется только физическими кнопками плитки.</p><pre id=status></pre></section>"
"<div class=grid><section><h2>Wi-Fi</h2><input id=ssid placeholder=SSID><input id=wpass type=password placeholder=Password>"
"<button onclick=wifi()>Save & connect</button></section>"
"<section id=settings><h2>Настройки</h2><label>Language <select id=lang><option value=0>English</option><option value=1>Русский</option><option value=2>简体中文</option></select></label>"
"<label><input id=sound type=checkbox checked> Sound</label><label><input id=context type=checkbox checked> Live data</label>"
"<label><input id=igbt type=checkbox> IGBT temperature</label><label><input id=tscreen type=checkbox> Timer screen always</label>"
"<label><input id=sclock type=checkbox checked> Clock during sleep</label><br>"
"<label>Sleep min <input id=sleep type=number min=1 max=60 value=1></label><label>OLED timeout <select id=oled>"
"<option value=60>1 min</option><option value=120>2 min</option><option value=180 selected>3 min</option>"
"<option value=300>5 min</option><option value=600>10 min</option><option value=1200>20 min</option>"
"<option value=1800>30 min</option><option value=3600>1 h</option><option value=7200>2 h</option>"
"<option value=10800>3 h</option><option value=14400>4 h</option><option value=18000>5 h</option></select></label>"
"<label>UTC offset, min <input id=timezone type=number min=-720 max=840 step=30 value=180></label>"
"<button onclick=saveSettings()>Save</button></section></div>"
"<section><h2>Profiles</h2><label># <input id=pindex type=number min=1 max=5 value=1 onchange=showProfile()></label>"
"<label>Name <input id=pname maxlength=11 value='PROFILE 1'></label>"
"<div class=stage><b>Stage 1</b><label>Mode <select id=pmode1><option value=0>POWER</option><option value=1>TEMPERATURE</option></select></label><label>Gear <input id=pgear1 type=number min=0 max=99 value=30></label><label>Temp °C <input id=ptemp1 type=number min=40 max=190 value=100></label><label>Time, min <input id=ptime1 type=number min=0 max=300 value=0></label></div>"
"<div class=stage><b>Stage 2</b><label>Mode <select id=pmode2><option value=0>POWER</option><option value=1>TEMPERATURE</option></select></label><label>Gear <input id=pgear2 type=number min=0 max=99 value=30></label><label>Temp °C <input id=ptemp2 type=number min=40 max=190 value=100></label><label>Time, min <input id=ptime2 type=number min=0 max=300 value=0></label></div>"
"<div class=stage><b>Stage 3</b><label>Mode <select id=pmode3><option value=0>POWER</option><option value=1>TEMPERATURE</option></select></label><label>Gear <input id=pgear3 type=number min=0 max=99 value=30></label><label>Temp °C <input id=ptemp3 type=number min=40 max=190 value=100></label><label>Time, min <input id=ptime3 type=number min=0 max=300 value=0></label></div>"
"<div class=stage><b>Stage 4</b><label>Mode <select id=pmode4><option value=0>POWER</option><option value=1>TEMPERATURE</option></select></label><label>Gear <input id=pgear4 type=number min=0 max=99 value=30></label><label>Temp °C <input id=ptemp4 type=number min=40 max=190 value=100></label><label>Time, min <input id=ptime4 type=number min=0 max=300 value=0></label></div>"
"<div class=stage><b>Stage 5</b><label>Mode <select id=pmode5><option value=0>POWER</option><option value=1>TEMPERATURE</option></select></label><label>Gear <input id=pgear5 type=number min=0 max=99 value=30></label><label>Temp °C <input id=ptemp5 type=number min=40 max=190 value=100></label><label>Time, min <input id=ptime5 type=number min=0 max=300 value=0></label></div>"
"<button onclick=saveProfile()>Save preset</button></section></main>"
"<script>let csrf='';const enc=o=>new URLSearchParams(o).toString(),el=id=>document.getElementById(id);"
"async function req(u,o={}){o.credentials='same-origin';o.headers=o.headers||{};if(o.method==='POST'){o.headers['Content-Type']='application/x-www-form-urlencoded';o.headers['X-CSRF-Token']=csrf}let r=await fetch(u,o);let t=await r.text();if(!r.ok)throw Error(t);return JSON.parse(t)}"
"async function login(){try{let j=await req('/api/login',{method:'POST',body:enc({password:pw.value})});csrf=j.csrf;auth.hidden=true;app.hidden=false;poll()}catch(e){alert(e)}}"
"async function setup(){try{let j=await req('/api/setup',{method:'POST',body:enc({password:pw.value})});csrf=j.csrf;auth.hidden=true;app.hidden=false;poll()}catch(e){alert(e)}}"
"async function wifi(){try{await req('/api/wifi',{method:'POST',body:enc({ssid:ssid.value,password:wpass.value})});alert('Saved')}catch(e){alert(e)}}"
"async function saveSettings(){try{await req('/api/settings',{method:'POST',body:enc({language:lang.value,sound:+sound.checked,context:+context.checked,igbt:+igbt.checked,timer_screen:+tscreen.checked,sleep_clock:+sclock.checked,sleep:sleep.value,oled:oled.value,timezone:timezone.value})});alert('Saved')}catch(e){alert(e)}}"
"async function saveProfile(){try{let o={index:pindex.value-1,name:pname.value};for(let i=1;i<=5;i++){o['mode'+i]=el('pmode'+i).value;o['gear'+i]=el('pgear'+i).value;o['temp'+i]=el('ptemp'+i).value;o['time'+i]=el('ptime'+i).value}await req('/api/profile',{method:'POST',body:enc(o)});alert('Saved')}catch(e){alert(e)}}"
"let profiles=[],loaded=false;function showProfile(){let p=profiles[Math.max(0,Math.min(4,pindex.value-1))];if(!p)return;pname.value=p.name;for(let i=1;i<=5;i++){let s=p.stages[i-1];el('pmode'+i).value=s[0];el('pgear'+i).value=s[1];el('ptemp'+i).value=s[2];el('ptime'+i).value=Math.floor(s[3]/60)}}"
"async function poll(){try{let j=await req('/api/status');status.textContent=JSON.stringify(j,null,2);net.textContent=!j.network.enabled?'Wi-Fi OFF':(j.network.sta_connected?'LAN '+j.network.ip:'Setup AP '+j.network.ap_ssid);profiles=j.profiles;if(!loaded){lang.value=j.settings.language;sound.checked=j.settings.sound;context.checked=j.settings.context;igbt.checked=j.settings.igbt;tscreen.checked=j.settings.timer_screen;sclock.checked=j.settings.sleep_clock;sleep.value=j.settings.sleep;oled.value=j.settings.oled;timezone.value=j.settings.timezone;showProfile();loaded=true}setTimeout(poll,1000)}catch(e){auth.hidden=false;app.hidden=true;net.textContent='Login required'}}poll();</script></html>";

static void random_hex(char output[33])
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); ++i) snprintf(output + i * 2, 3, "%02x", bytes[i]);
}

static esp_err_t json(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    char body[160];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", message);
    return json(req, body);
}

static bool cookie_authorized(httpd_req_t *req)
{
    if (!settings_admin_is_configured() || s_session[0] == 0 ||
        esp_timer_get_time() >= s_session_deadline_us) return false;
    size_t length = httpd_req_get_hdr_value_len(req, "Cookie");
    if (length == 0 || length > 255) return false;
    char cookie[256];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) return false;
    const char *token = strstr(cookie, "MCLSESSION=");
    if (token == NULL) return false;
    token += strlen("MCLSESSION=");
    return strncmp(token, s_session, 32) == 0 && (token[32] == 0 || token[32] == ';');
}

static bool csrf_authorized(httpd_req_t *req)
{
    if (!cookie_authorized(req)) return false;
    size_t length = httpd_req_get_hdr_value_len(req, "X-CSRF-Token");
    if (length != 32) return false;
    char value[40];
    return httpd_req_get_hdr_value_str(req, "X-CSRF-Token", value, sizeof(value)) == ESP_OK &&
           strcmp(value, s_csrf) == 0;
}

static esp_err_t receive_body(httpd_req_t *req, char body[BODY_MAX])
{
    if (req->content_len <= 0 || req->content_len >= BODY_MAX) return ESP_ERR_INVALID_SIZE;
    int received = 0;
    while (received < req->content_len) {
        const int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    body[received] = 0;
    return ESP_OK;
}

static void url_decode(char *text)
{
    char *read = text, *write = text;
    while (*read) {
        if (*read == '+') { *write++ = ' '; ++read; }
        else if (*read == '%' && isxdigit((unsigned char)read[1]) && isxdigit((unsigned char)read[2])) {
            char hex[3] = {read[1], read[2], 0};
            *write++ = (char)strtol(hex, NULL, 16);
            read += 3;
        } else *write++ = *read++;
    }
    *write = 0;
}

static bool form_value(const char *body, const char *key, char *output, size_t output_size)
{
    const size_t key_len = strlen(key);
    const char *at = body;
    while (at && *at) {
        if ((at == body || at[-1] == '&') && strncmp(at, key, key_len) == 0 && at[key_len] == '=') {
            at += key_len + 1;
            const char *end = strchr(at, '&');
            size_t length = end ? (size_t)(end - at) : strlen(at);
            if (length >= output_size) return false;
            memcpy(output, at, length);
            output[length] = 0;
            url_decode(output);
            return true;
        }
        at = strchr(at, '&');
        if (at) ++at;
    }
    return false;
}

static int form_int(const char *body, const char *key, int fallback)
{
    char value[32];
    if (!form_value(body, key, value, sizeof(value))) return fallback;
    errno = 0;
    char *end = NULL;
    const long parsed = strtol(value, &end, 10);
    return errno == 0 && end != value && *end == 0 && parsed >= INT_MIN && parsed <= INT_MAX ?
           (int)parsed : fallback;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t issue_session(httpd_req_t *req)
{
    random_hex(s_session);
    random_hex(s_csrf);
    s_session_deadline_us = esp_timer_get_time() + 12LL * 60 * 60 * 1000000;
    char cookie[96];
    snprintf(cookie, sizeof(cookie), "MCLSESSION=%s; HttpOnly; SameSite=Strict; Path=/", s_session);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    char response[96];
    snprintf(response, sizeof(response), "{\"ok\":true,\"csrf\":\"%s\"}", s_csrf);
    return json(req, response);
}

static esp_err_t login_handler(httpd_req_t *req)
{
    if (esp_timer_get_time() < s_login_blocked_until_us)
        return error(req, "429 Too Many Requests", "login temporarily blocked");
    char body[BODY_MAX], password[64];
    if (receive_body(req, body) != ESP_OK || !form_value(body, "password", password, sizeof(password)))
        return error(req, "400 Bad Request", "password required");
    if (!settings_admin_verify(password)) {
        ++s_login_failures;
        if (s_login_failures >= 5) {
            const unsigned seconds = s_login_failures >= 9 ? 30 : 2U << (s_login_failures - 5);
            s_login_blocked_until_us = esp_timer_get_time() + (int64_t)seconds * 1000000;
        }
        return error(req, "401 Unauthorized", "bad password");
    }
    s_login_failures = 0;
    s_login_blocked_until_us = 0;
    return issue_session(req);
}

static esp_err_t setup_handler(httpd_req_t *req)
{
    if (settings_admin_is_configured()) return error(req, "409 Conflict", "already configured");
    char body[BODY_MAX], password[64];
    if (receive_body(req, body) != ESP_OK || !form_value(body, "password", password, sizeof(password)))
        return error(req, "400 Bad Request", "password required");
    const esp_err_t err = settings_admin_set(password);
    if (err != ESP_OK) return error(req, "400 Bad Request", esp_err_to_name(err));
    return issue_session(req);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!cookie_authorized(req)) return error(req, "401 Unauthorized", "login required");
    char cooker[896];
    cooking_engine_status_json(cooker, sizeof(cooker));
    char powerboard[1792];
    powerboard_control_status_json(powerboard, sizeof(powerboard));
    network_status_t network;
    network_prod_get_status(&network);
    app_settings_t settings;
    settings_get(&settings);
    cooker_profile_t profiles[COOKER_PROFILE_COUNT];
    settings_profiles_get(profiles);
    char profiles_json[1100] = "[";
    size_t profiles_used = 1;
    for (unsigned i = 0; i < COOKER_PROFILE_COUNT; ++i) {
        int written = snprintf(profiles_json + profiles_used,
                               sizeof(profiles_json) - profiles_used,
                               "%s{\"name\":\"%s\",\"stages\":[",
                               i ? "," : "", profiles[i].name);
        if (written < 0 || (size_t)written >= sizeof(profiles_json) - profiles_used)
            return error(req, "500 Internal Server Error", "profile json overflow");
        profiles_used += (size_t)written;
        for (unsigned stage = 0; stage < COOKER_PROFILE_STAGE_COUNT; ++stage) {
            const cooker_profile_stage_t *cell = &profiles[i].stages[stage];
            written = snprintf(profiles_json + profiles_used,
                               sizeof(profiles_json) - profiles_used,
                               "%s[%u,%u,%u,%" PRIu32 "]",
                               stage ? "," : "", cell->mode, cell->gear,
                               cell->temperature_c, cell->timer_s);
            if (written < 0 || (size_t)written >= sizeof(profiles_json) - profiles_used)
                return error(req, "500 Internal Server Error", "profile json overflow");
            profiles_used += (size_t)written;
        }
        written = snprintf(profiles_json + profiles_used,
                           sizeof(profiles_json) - profiles_used, "]}");
        if (written < 0 || (size_t)written >= sizeof(profiles_json) - profiles_used)
            return error(req, "500 Internal Server Error", "profile json overflow");
        profiles_used += (size_t)written;
    }
    strlcpy(profiles_json + profiles_used, "]", sizeof(profiles_json) - profiles_used);
    char response[5120];
    const int response_length = snprintf(response, sizeof(response),
             "{\"firmware\":\"%s\",\"cooker\":%s,\"powerboard\":%s,"
             "\"network\":{\"enabled\":%s,\"sta_connected\":%s,\"ip\":\"%s\","
             "\"ap_ssid\":\"%s\",\"clock\":%s},"
             "\"settings\":{\"language\":%u,\"sound\":%s,\"context\":%s,"
             "\"igbt\":%s,\"timer_screen\":%s,\"sleep_clock\":%s,"
             "\"i2c_debug\":%s,"
             "\"wifi_enabled\":%s,\"sleep\":%u,\"oled\":%u,"
             "\"timezone\":%d},\"profiles\":%s,\"persistence\":%s,"
             "\"telemetry_dropped\":%" PRIu32 "}",
             MCL02M_FIRMWARE_VERSION, cooker, powerboard,
             network.enabled ? "true" : "false",
             network.sta_connected ? "true" : "false", network.sta_ip,
             network.ap_ssid, network.clock_synchronized ? "true" : "false",
             settings.language, settings.sound_enabled ? "true" : "false",
             settings.show_context_value ? "true" : "false",
             settings.show_igbt ? "true" : "false",
             settings.timer_screen_mode == TIMER_SCREEN_ALWAYS ? "true" : "false",
             settings.show_sleep_clock ? "true" : "false",
             settings.show_i2c_debug ? "true" : "false",
             settings.wifi_enabled ? "true" : "false",
             settings.sleep_minutes, settings.oled_timeout_s, settings.timezone_minutes,
             profiles_json,
             settings_persistence_available() ? "true" : "false",
             telemetry_dropped_count());
    if (response_length < 0 || (size_t)response_length >= sizeof(response))
        return error(req, "500 Internal Server Error", "status json overflow");
    return json(req, response);
}

static esp_err_t wifi_handler(httpd_req_t *req)
{
    if (!csrf_authorized(req)) return error(req, "403 Forbidden", "auth/csrf");
    char body[BODY_MAX], ssid[33], password[64];
    if (receive_body(req, body) != ESP_OK || !form_value(body, "ssid", ssid, sizeof(ssid)) ||
        !form_value(body, "password", password, sizeof(password)))
        return error(req, "400 Bad Request", "ssid/password required");
    const esp_err_t err = network_prod_configure(ssid, password);
    return err == ESP_OK ? json(req, "{\"ok\":true}") :
           error(req, "400 Bad Request", esp_err_to_name(err));
}

static esp_err_t settings_handler(httpd_req_t *req)
{
    if (!csrf_authorized(req)) return error(req, "403 Forbidden", "auth/csrf");
    char body[BODY_MAX];
    if (receive_body(req, body) != ESP_OK) return error(req, "400 Bad Request", "body");
    app_settings_t settings;
    settings_get(&settings);
    settings.language = form_int(body, "language", settings.language);
    settings.sound_enabled = form_int(body, "sound", settings.sound_enabled) != 0;
    settings.show_context_value = form_int(body, "context", settings.show_context_value) != 0;
    settings.show_igbt = form_int(body, "igbt", settings.show_igbt) != 0;
    settings.timer_screen_mode = form_int(body, "timer_screen", 0) ? TIMER_SCREEN_ALWAYS : TIMER_SCREEN_AUTO;
    settings.show_sleep_clock = form_int(body, "sleep_clock", settings.show_sleep_clock) != 0;
    settings.sleep_minutes = form_int(body, "sleep", settings.sleep_minutes);
    settings.oled_timeout_s = form_int(body, "oled", settings.oled_timeout_s);
    settings.timezone_minutes = form_int(body, "timezone", settings.timezone_minutes);
    const esp_err_t err = settings_update(&settings);
    if (err == ESP_OK) {
        sound_set_enabled(settings.sound_enabled);
        network_prod_apply_timezone();
    }
    return err == ESP_OK ? json(req, "{\"ok\":true}") :
           error(req, "400 Bad Request", esp_err_to_name(err));
}

static esp_err_t profile_handler(httpd_req_t *req)
{
    if (!csrf_authorized(req)) return error(req, "403 Forbidden", "auth/csrf");
    char body[BODY_MAX], name[12] = {0};
    if (receive_body(req, body) != ESP_OK || !form_value(body, "name", name, sizeof(name)))
        return error(req, "400 Bad Request", "profile body");
    const int index = form_int(body, "index", -1);
    if (index < 0 || index >= COOKER_PROFILE_COUNT)
        return error(req, "400 Bad Request", "profile index");
    cooker_profile_t profile = {0};
    strlcpy(profile.name, name, sizeof(profile.name));
    uint32_t total_s = 0;
    for (unsigned stage = 0; stage < COOKER_PROFILE_STAGE_COUNT; ++stage) {
        char key[12];
        snprintf(key, sizeof(key), "mode%u", stage + 1U);
        const int mode = form_int(body, key, 0);
        snprintf(key, sizeof(key), "gear%u", stage + 1U);
        const int gear = form_int(body, key, 30);
        snprintf(key, sizeof(key), "temp%u", stage + 1U);
        const int temperature = form_int(body, key, 100);
        snprintf(key, sizeof(key), "time%u", stage + 1U);
        const int minutes = form_int(body, key, 0);
        if (mode < 0 || mode > COOK_MODE_TEMPERATURE || gear < 0 || gear > 99 ||
            temperature < COOKER_TEMP_MIN_C || temperature > COOKER_TEMP_MAX_C ||
            minutes < 0 || minutes > 300)
            return error(req, "400 Bad Request", "profile stage range");
        const uint32_t seconds = (uint32_t)minutes * 60U;
        if (total_s + seconds > COOKER_MAX_TIMER_S)
            return error(req, "400 Bad Request", "profile total exceeds 5 h");
        total_s += seconds;
        profile.stages[stage].mode = (uint8_t)mode;
        profile.stages[stage].gear = (uint8_t)gear;
        profile.stages[stage].temperature_c = (uint16_t)temperature;
        profile.stages[stage].timer_s = seconds;
    }
    const esp_err_t err = settings_profile_set((unsigned)index, &profile);
    return err == ESP_OK ? json(req, "{\"ok\":true}") :
           error(req, "400 Bad Request", esp_err_to_name(err));
}

static const httpd_uri_t ROUTES[] = {
    {.uri = "/", .method = HTTP_GET, .handler = root_handler},
    {.uri = "/api/login", .method = HTTP_POST, .handler = login_handler},
    {.uri = "/api/setup", .method = HTTP_POST, .handler = setup_handler},
    {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
    {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_handler},
    {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_handler},
    {.uri = "/api/profile", .method = HTTP_POST, .handler = profile_handler},
};

esp_err_t web_server_prod_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240;
    config.max_uri_handlers = 12;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    for (unsigned i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &ROUTES[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
