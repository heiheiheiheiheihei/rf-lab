#include "web_server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "codebook.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "net.h"
#include "ota.h"

#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.7.1"
#endif
#ifndef RFLAB_BUILD_ID
#define RFLAB_BUILD_ID "dev"
#endif
#ifndef RFLAB_UI_HASH
#define RFLAB_UI_HASH "unknown"
#endif

/* scripts/embed_web.py 生成 web_ui_gz.S(.incbin web/index.html.gz)的符号。 */
extern const uint8_t web_ui_gz_start[];
extern const uint8_t web_ui_gz_end[];

#define SSE_MAX_CLIENTS 2
#define HTTP_MAX_BODY 384

static volatile int s_sse_clients;

static void cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t send_json(httpd_req_t *req, const char *body)
{
    cors(req);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get(httpd_req_t *req)
{
    size_t len = (size_t)(web_ui_gz_end - web_ui_gz_start);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)web_ui_gz_start, len);
}

static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t hello_get(httpd_req_t *req)
{
    net_info_t ni;
    net_get_info(&ni);
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"fw\":\"%s\",\"build\":\"%s\",\"ui\":\"%s\",\"proto\":1,\"codebook\":%d,\"name\":\"%s\","
             "\"ap_ssid\":\"%s\",\"ip\":\"%s\",\"auth\":1}",
             RFLAB_FW_VERSION, RFLAB_BUILD_ID, RFLAB_UI_HASH, CODEBOOK_VERSION,
             app_name(), ni.ap_ssid, net_ip());
    return send_json(req, buf);
}

static esp_err_t state_get(httpd_req_t *req)
{
    char buf[APP_STATE_JSON_MAX];
    app_copy_state(buf, sizeof(buf));
    return send_json(req, buf);
}

static esp_err_t codes_get(httpd_req_t *req)
{
    /* 6144B 超过 HTTP task 的 6KB 栈:按请求分配,避免栈溢出导致连接被重置。 */
    char *buf = malloc(APP_CODES_JSON_MAX);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"oom\"}");
    }
    app_copy_codes(buf, APP_CODES_JSON_MAX);
    esp_err_t r = send_json(req, buf);
    free(buf);
    return r;
}

static esp_err_t net_get(httpd_req_t *req)
{
    net_info_t ni;
    net_get_info(&ni);
    char buf[320];
    snprintf(buf, sizeof(buf),
             "{\"mode\":\"%s\",\"ap_on\":%s,\"has_creds\":%s,"
             "\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"ap_clients\":%d,"
             "\"sta_connected\":%s,\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\",\"rssi\":%d}",
             ni.mode, ni.ap_on ? "true" : "false", ni.has_creds ? "true" : "false",
             ni.ap_ssid, ni.ap_ip, ni.ap_clients,
             ni.sta_connected ? "true" : "false", ni.sta_ssid, ni.sta_ip, ni.rssi);
    return send_json(req, buf);
}

static int read_body(httpd_req_t *req, char *buf, size_t cap)
{
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= cap) {
        return -1;
    }
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, (size_t)(total - got));
        if (r <= 0) {
            return -1;
        }
        got += r;
    }
    buf[got] = 0;
    return got;
}

/* 极简 JSON 字符串取值:只处理 "key":"value",支持 \" \\ \n \t \r。 */
static int json_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;

    size_t o = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            default: return -1;
            }
        }
        if (o + 1 >= cap) {
            return -1;
        }
        out[o++] = c;
    }
    if (*p != '"') {
        return -1;
    }
    out[o] = 0;
    return (int)o;
}

static esp_err_t cmd_post(httpd_req_t *req)
{
    char body[HTTP_MAX_BODY];
    if (read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return send_json(req, "{\"ok\":false,\"error\":\"body\"}");
    }

    char cmd[APP_CMD_MAX];
    cmd[0] = 0;
    if (body[0] == '{') {
        if (json_str(body, "cmd", cmd, sizeof(cmd)) < 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            return send_json(req, "{\"ok\":false,\"error\":\"cmd\"}");
        }
    } else {
        /* 明文命令:cmd 缓冲 96B,body 可到 383B;显式截断,避免
         * 新版 GCC 把「可能截断」升级成 -Werror=format-truncation。 */
        snprintf(cmd, sizeof(cmd), "%.*s", (int)sizeof(cmd) - 1, body);
    }

    if (cmd[0] == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"ok\":false,\"error\":\"empty\"}");
    }

    int rc = app_submit_cmd(cmd, APP_SRC_HTTP);
    if (rc != 0) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"ok\":false,\"error\":\"queue full\"}");
    }

    httpd_resp_set_status(req, "202 Accepted");
    return send_json(req, "{\"ok\":true,\"queued\":true}");
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[HTTP_MAX_BODY];
    if (read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return send_json(req, "{\"ok\":false,\"error\":\"body\"}");
    }

    char ssid[33];
    char pass[65];
    if (json_str(body, "ssid", ssid, sizeof(ssid)) < 0 ||
        json_str(body, "pass", pass, sizeof(pass)) < 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"ok\":false,\"error\":\"ssid/pass\"}");
    }

    if (strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(pass) > 64) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"ok\":false,\"error\":\"invalid\"}");
    }

    /* 先把 202 发出去,再切模式/断 STA;否则在 STA 模式下配网会先把这条 TCP 掐掉。 */
    httpd_resp_set_status(req, "202 Accepted");
    esp_err_t r = send_json(req, "{\"ok\":true,\"connecting\":true}");
    net_set_sta(ssid, pass);
    return r;
}

static esp_err_t wifi_clear_post(httpd_req_t *req)
{
    /* 同上:先回 202,再断开 STA/切 AP。 */
    httpd_resp_set_status(req, "202 Accepted");
    esp_err_t r = send_json(req, "{\"ok\":true,\"mode\":\"ap\"}");
    net_clear_sta();
    return r;
}

/* Firmware upload. Token is armed from the serial console only (`OTA`);
 * a successful upload writes the inactive slot and main reboots after a
 * short grace period. No token => 401, no free slot => 409. */
static esp_err_t ota_post(httpd_req_t *req)
{
    char token[16];
    if (httpd_req_get_hdr_value_str(req, "X-RFLAB-OTA", token, sizeof(token)) != ESP_OK ||
        !ota_check_token(token)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_json(req, "{\"ok\":false,\"error\":\"bad or missing token: serial OTA\"}");
    }

    int total = req->content_len;
    if (total <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"ok\":false,\"error\":\"content-length required\"}");
    }

    esp_err_t e = ota_begin((size_t)total);
    if (e != ESP_OK) {
        char err[160];
        snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", ota_error());
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, err);
    }

    char *buf = malloc(4096);
    if (!buf) {
        ota_abort();
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"ok\":false,\"error\":\"out of memory\"}");
    }

    int got = 0;
    int timeouts = 0;
    while (got < total) {
        size_t want = (size_t)(total - got);
        if (want > 4096) {
            want = 4096;
        }
        int r = httpd_req_recv(req, buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            ota_abort();
            httpd_resp_set_status(req, "400 Bad Request");
            return send_json(req, "{\"ok\":false,\"error\":\"upload interrupted\"}");
        }
        timeouts = 0;
        e = ota_write(buf, (size_t)r);
        if (e != ESP_OK) {
            free(buf);
            ota_abort();
            char err[160];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", ota_error());
            httpd_resp_set_status(req, "500 Internal Server Error");
            return send_json(req, err);
        }
        got += r;
    }
    free(buf);

    e = ota_finish();
    if (e != ESP_OK) {
        char err[160];
        snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", ota_error());
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, err);
    }

    char ok[128];
    snprintf(ok, sizeof(ok),
             "{\"ok\":true,\"bytes\":%d,\"slot\":%d,\"reboot\":true}", total, ota_target_slot());
    return send_json(req, ok);
}

static bool sse_send(httpd_req_t *req, const char *event, const char *data)
{
    char head[48];
    int n = snprintf(head, sizeof(head), "event: %s\ndata: ", event);
    if (httpd_resp_send_chunk(req, head, n) != ESP_OK) {
        return false;
    }
    if (httpd_resp_send_chunk(req, data, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return false;
    }
    return httpd_resp_send_chunk(req, "\n\n", 2) == ESP_OK;
}

typedef struct {
    char state[APP_STATE_JSON_MAX];
    char line[APP_STATE_JSON_MAX + 96];
    char ev[LINK_LINE_MAX];
} sse_ctx_t;

static void sse_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;
    sse_ctx_t *ctx = calloc(1, sizeof(sse_ctx_t));
    char hello[384];
    uint32_t cursor = link_head();
    uint32_t st_seq = 0;
    uint32_t cd_seq = 0;
    int idle = 0;

    if (!ctx) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "oom", HTTPD_RESP_USE_STRLEN);
        httpd_req_async_handler_complete(req);
        s_sse_clients--;
        vTaskDelete(NULL);
        return;
    }

    httpd_resp_set_type(req, "text/event-stream; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    net_info_t ni;
    net_get_info(&ni);
    snprintf(hello, sizeof(hello),
             "{\"t\":\"hello\",\"fw\":\"%s\",\"build\":\"%s\",\"ui\":\"%s\",\"proto\":1,"
             "\"codebook\":%d,\"name\":\"%s\",\"ap_ssid\":\"%s\",\"ip\":\"%s\",\"auth\":1}",
             RFLAB_FW_VERSION, RFLAB_BUILD_ID, RFLAB_UI_HASH, CODEBOOK_VERSION,
             app_name(), ni.ap_ssid, net_ip());
    if (!sse_send(req, "hello", hello)) {
        goto done;
    }

    for (;;) {
        bool did = false;

        for (;;) {
            uint32_t seq = 0;
            int r = link_read(&cursor, ctx->ev, sizeof(ctx->ev), &seq);
            if (r == 0) {
                break;
            }
            if (r == 1 && !sse_send(req, "event", ctx->ev)) {
                goto done;
            }
            did = true;
        }

        if (app_state_seq() != st_seq) {
            app_copy_state(ctx->state, sizeof(ctx->state));
            snprintf(ctx->line, sizeof(ctx->line), "{\"t\":\"state\",\"seq\":%lu,\"data\":%s}",
                     (unsigned long)app_state_seq(), ctx->state);
            if (!sse_send(req, "state", ctx->line)) {
                goto done;
            }
            st_seq = app_state_seq();
            did = true;
        }

        if (app_codes_seq() != cd_seq) {
            /* 直接流式发送 main 的只读快照,ctx 里不再放 12KB 副本:
             * 低内存时 SSE calloc 才不会 OOM。 */
            const char *codes = app_lock_codes();
            if (!codes) {
                goto done;
            }
            char hdr[96];
            int h = snprintf(hdr, sizeof(hdr),
                             "{\"t\":\"codes\",\"seq\":%lu,\"items\":",
                             (unsigned long)app_codes_seq());
            bool ok = h > 0 &&
                      httpd_resp_send_chunk(req, "event: codes\ndata: ",
                                            HTTPD_RESP_USE_STRLEN) == ESP_OK &&
                      httpd_resp_send_chunk(req, hdr, (size_t)h) == ESP_OK &&
                      httpd_resp_send_chunk(req, codes, HTTPD_RESP_USE_STRLEN) == ESP_OK &&
                      httpd_resp_send_chunk(req, "}\n\n", 3) == ESP_OK;
            app_unlock_codes();
            if (!ok) {
                goto done;
            }
            cd_seq = app_codes_seq();
            did = true;
        }

        if (!did && ++idle >= 50) {
            if (httpd_resp_send_chunk(req, ": ping\n\n", 8) != ESP_OK) {
                goto done;
            }
            idle = 0;
        } else if (did) {
            idle = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

done:
    httpd_resp_send_chunk(req, NULL, 0);
    httpd_req_async_handler_complete(req);
    s_sse_clients--;
    free(ctx);
    vTaskDelete(NULL);
}

static esp_err_t events_get(httpd_req_t *req)
{
    if (s_sse_clients >= SSE_MAX_CLIENTS) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "too many event streams", HTTPD_RESP_USE_STRLEN);
    }

    httpd_req_t *async = NULL;
    esp_err_t e = httpd_req_async_handler_begin(req, &async);
    if (e != ESP_OK) {
        return e;
    }

    s_sse_clients++;
    if (xTaskCreate(sse_task, "sse", 6144, async, 4, NULL) != pdPASS) {
        s_sse_clients--;
        httpd_req_async_handler_complete(async);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static const httpd_uri_t s_uris[] = {
    {.uri = "/", .method = HTTP_GET, .handler = root_get},
    {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get},
    {.uri = "/api/hello", .method = HTTP_GET, .handler = hello_get},
    {.uri = "/api/state", .method = HTTP_GET, .handler = state_get},
    {.uri = "/api/codes", .method = HTTP_GET, .handler = codes_get},
    {.uri = "/api/net", .method = HTTP_GET, .handler = net_get},
    {.uri = "/api/events", .method = HTTP_GET, .handler = events_get},
    {.uri = "/api/cmd", .method = HTTP_POST, .handler = cmd_post},
    {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post},
    {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post},
    {.uri = "/api/wifi/clear", .method = HTTP_POST, .handler = wifi_clear_post},
};

esp_err_t web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_open_sockets = 5;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;

    httpd_handle_t server = NULL;
    esp_err_t e = httpd_start(&server, &cfg);
    if (e != ESP_OK) {
        return e;
    }
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
        e = httpd_register_uri_handler(server, &s_uris[i]);
        if (e != ESP_OK) {
            return e;
        }
    }
    return ESP_OK;
}
