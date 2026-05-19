/**
 * @file wifi_portal.c
 * @brief SoftAP 配网 + STA 自动联网实现
 *
 * 模块职责：
 * - 启动固定 SoftAP，提供网页配置入口
 * - 从表单接收 SSID/密码并写入 NVS
 * - 按保存的配置发起 STA 连接
 * - 提供一个简单状态页，方便查看当前连接状态
 *
 * 状态策略：
 * - AP 始终保留，避免“配错 Wi-Fi 后无法再进入配置页”
 * - STA 连接失败只记录日志，不影响当前离线唤醒/命令词功能
 * - 重配置时通过 reconnect_requested 串行切换，避免重复 connect() 导致抖动
 */
#include "wifi_portal.h"

#include "sdkconfig.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_MAXIMUM_RETRY 5
#define WIFI_NAMESPACE "wifi_cfg"
#define WIFI_KEY_SSID "sta_ssid"
#define WIFI_KEY_PASS "sta_pass"
#define WIFI_STATUS_BUF_LEN 256
#define WIFI_INDEX_PAGE_BUF_LEN 3072
#define WIFI_HTTPD_STACK_SIZE 8192

static const char *TAG = "wifi_portal";

/* AP/STA 双网卡对象。AP 负责配置入口，STA 负责连接外部路由器。 */
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static httpd_handle_t s_server;

/* 连接状态：
 * - connected: 已拿到 IP
 * - connecting: 已发起 connect()，等待事件回调
 * - configured: NVS 中已有可用 SSID
 * - reconnect_requested: 配置切换时，等待 DISCONNECTED 事件后再 connect()
 */
static bool s_sta_connected;
static bool s_sta_connecting;
static bool s_sta_configured;
static bool s_wifi_stack_ready;
static bool s_reconnect_requested;
static int s_retry_num;

/* 最近一次保存的 STA 配置与当前 IP。 */
static char s_sta_ssid[33];
static char s_sta_password[65];
static char s_sta_ip[16] = "0.0.0.0";

/* 发起一次 STA 连接或重连。 */
static esp_err_t wifi_portal_connect_sta(void);
static void trim_ascii_whitespace(char *value);
static bool apply_default_sta_credentials(void);

/* ────────────────────── 页面/表单辅助函数 ────────────────────── */

/* 将字符串转成可安全插入 HTML 的形式，避免状态页里出现非法标签。 */
static void html_escape_append(char *dst, size_t dst_size, size_t *offset, const char *src)
{
    while (*src != '\0' && *offset + 1 < dst_size) {
        const char *replace = NULL;

        switch (*src) {
        case '&':
            replace = "&amp;";
            break;
        case '<':
            replace = "&lt;";
            break;
        case '>':
            replace = "&gt;";
            break;
        case '"':
            replace = "&quot;";
            break;
        default:
            break;
        }

        if (replace != NULL) {
            int written = snprintf(dst + *offset, dst_size - *offset, "%s", replace);
            if (written <= 0 || (size_t)written >= dst_size - *offset) {
                *offset = dst_size - 1;
                dst[*offset] = '\0';
                return;
            }
            *offset += (size_t)written;
        } else {
            dst[(*offset)++] = *src;
            dst[*offset] = '\0';
        }
        src++;
    }
}

/* 生成状态页使用的简易 JSON。 */
static void set_sta_status_json(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size,
             "{\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"sta_connected\":%s,"
             "\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\"}",
             WIFI_PORTAL_AP_SSID,
             WIFI_PORTAL_AP_IP,
             s_sta_connected ? "true" : "false",
             s_sta_ssid,
             s_sta_ip);
}

/* 从 NVS 读取上一次保存的 Wi-Fi 凭据。 */
static esp_err_t nvs_load_wifi_credentials(void)
{
    nvs_handle_t nvs_handle;
    size_t ssid_len = sizeof(s_sta_ssid);
    size_t pass_len = sizeof(s_sta_password);

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs_handle, WIFI_KEY_SSID, s_sta_ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, WIFI_KEY_PASS, s_sta_password, &pass_len);
    }

    nvs_close(nvs_handle);
    if (err == ESP_OK) {
        trim_ascii_whitespace(s_sta_ssid);
        trim_ascii_whitespace(s_sta_password);
        s_sta_configured = (s_sta_ssid[0] != '\0');
    }
    return err;
}

/* 将新的 Wi-Fi 凭据持久化到 NVS。 */
static esp_err_t nvs_save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, WIFI_KEY_PASS, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

/* URL 解码辅助：将 0-9 / a-f / A-F 转为数值。 */
static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

/* 将 application/x-www-form-urlencoded 字符串还原成普通文本。 */
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t di = 0;

    while (*src != '\0' && di + 1 < dst_size) {
        if (*src == '+') {
            dst[di++] = ' ';
            src++;
            continue;
        }

        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int hi = hex_to_int(src[1]);
            int lo = hex_to_int(src[2]);
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                src += 3;
                continue;
            }
        }

        dst[di++] = *src++;
    }

    dst[di] = '\0';
}

/* 从 POST 表单体中提取指定 key 的值。 */
static void parse_form_value(const char *body, const char *key, char *out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char *cursor = body;

    out[0] = '\0';

    while (cursor != NULL && *cursor != '\0') {
        const char *item_end = strchr(cursor, '&');
        size_t item_len = item_end ? (size_t)(item_end - cursor) : strlen(cursor);

        if (item_len > key_len + 1 && strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=') {
            char encoded[256];
            size_t copy_len = item_len - key_len - 1;

            if (copy_len >= sizeof(encoded)) {
                copy_len = sizeof(encoded) - 1;
            }

            memcpy(encoded, cursor + key_len + 1, copy_len);
            encoded[copy_len] = '\0';
            url_decode(out, encoded, out_size);
            return;
        }

        cursor = item_end ? item_end + 1 : NULL;
    }
}

/* 清理表单首尾空格，避免 "Pro " 这种输入误保存成错误 SSID。 */
static void trim_ascii_whitespace(char *value)
{
    size_t len;
    size_t start = 0;

    if (value == NULL || value[0] == '\0') {
        return;
    }

    len = strlen(value);
    while (start < len && isspace((unsigned char)value[start])) {
        start++;
    }

    while (len > start && isspace((unsigned char)value[len - 1])) {
        len--;
    }

    if (start > 0) {
        memmove(value, value + start, len - start);
    }
    value[len - start] = '\0';
}

/* 在没有 NVS 保存值时，应用编译期默认 Wi-Fi。
 * 这样 demo 阶段可以开机直连；用户之后仍可通过网页覆盖。 */
static bool apply_default_sta_credentials(void)
{
#if CONFIG_WIFI_PORTAL_USE_DEFAULT_STA
    if (strlen(CONFIG_WIFI_PORTAL_DEFAULT_STA_SSID) == 0) {
        ESP_LOGW(TAG, "default STA is enabled but SSID is empty");
        return false;
    }

    strlcpy(s_sta_ssid, CONFIG_WIFI_PORTAL_DEFAULT_STA_SSID, sizeof(s_sta_ssid));
    strlcpy(s_sta_password, CONFIG_WIFI_PORTAL_DEFAULT_STA_PASSWORD, sizeof(s_sta_password));
    trim_ascii_whitespace(s_sta_ssid);
    trim_ascii_whitespace(s_sta_password);

    if (s_sta_ssid[0] == '\0') {
        ESP_LOGW(TAG, "default STA SSID becomes empty after trimming");
        return false;
    }

    s_sta_configured = true;
    ESP_LOGI(TAG, "using built-in default Wi-Fi for SSID: %s", s_sta_ssid);

    esp_err_t err = nvs_save_wifi_credentials(s_sta_ssid, s_sta_password);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save built-in default Wi-Fi to NVS failed: %s", esp_err_to_name(err));
    }
    return true;
#else
    return false;
#endif
}

/* 浏览器通常会额外请求 favicon，这里直接返回空响应避免日志刷屏。 */
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* 返回当前 AP/STA 状态的 JSON。 */
static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    char buffer[WIFI_STATUS_BUF_LEN];

    set_sta_status_json(buffer, sizeof(buffer));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buffer);
}

/* 返回配置主页。主页内容较大，使用堆内存避免压坏 HTTP 任务栈。 */
static esp_err_t wifi_index_handler(httpd_req_t *req)
{
    char ssid_html[128];
    char *page = malloc(WIFI_INDEX_PAGE_BUF_LEN);
    size_t ssid_offset = 0;
    const char *connection_state = s_sta_connected ? "已连接" : (s_sta_configured ? "连接中/未连上" : "未配置");

    if (page == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_ERR_NO_MEM;
    }

    ssid_html[0] = '\0';
    html_escape_append(ssid_html, sizeof(ssid_html), &ssid_offset, s_sta_ssid);

    int written = snprintf(
        page, WIFI_INDEX_PAGE_BUF_LEN,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Skainet Wi-Fi</title>"
        "<style>body{font-family:sans-serif;max-width:720px;margin:24px auto;padding:0 16px;}"
        "input{width:100%%;padding:12px;margin:8px 0;box-sizing:border-box;}"
        "button{padding:12px 18px;}code{background:#f4f4f4;padding:2px 6px;border-radius:4px;}"
        ".card{border:1px solid #ddd;border-radius:10px;padding:16px;margin-bottom:18px;}"
        "</style></head><body>"
        "<h1>Skainet Wi-Fi 配置</h1>"
        "<div class=\"card\"><p>连接设备 AP：<code>%s</code></p>"
        "<p>AP 密码：<code>%s</code></p>"
        "<p>固定地址：<a href=\"http://%s\">http://%s</a></p>"
        "<p>STA 状态：<strong>%s</strong></p>"
        "<p>当前 SSID：<strong>%s</strong></p>"
        "<p>STA IP：<strong>%s</strong></p></div>"
        "<div class=\"card\"><form method=\"post\" action=\"/configure\">"
        "<label>路由器 SSID</label><input name=\"ssid\" maxlength=\"32\" required value=\"%s\">"
        "<label>路由器密码</label><input name=\"password\" maxlength=\"64\" type=\"password\" value=\"\">"
        "<button type=\"submit\">保存并连接</button></form></div>"
        "<p>保存后设备会继续保留配置 AP，同时尝试连接你填写的路由器。</p>"
        "<p><a href=\"/status\">查看 JSON 状态</a></p>"
        "</body></html>",
        WIFI_PORTAL_AP_SSID,
        WIFI_PORTAL_AP_PASSWORD,
        WIFI_PORTAL_AP_IP,
        WIFI_PORTAL_AP_IP,
        connection_state,
        s_sta_configured ? ssid_html : "未配置",
        s_sta_ip,
        s_sta_configured ? ssid_html : "");

    if (written < 0 || (size_t)written >= WIFI_INDEX_PAGE_BUF_LEN) {
        free(page);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, page, written);
    free(page);
    return err;
}

/* 处理网页提交的 Wi-Fi 参数：保存到 NVS，并触发 STA 连接。 */
static esp_err_t wifi_configure_handler(httpd_req_t *req)
{
    char body[384];
    char ssid[33];
    char password[65];
    int total_len = req->content_len;

    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        return ESP_FAIL;
    }

    int recv_len = httpd_req_recv(req, body, total_len);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        return ESP_FAIL;
    }

    body[recv_len] = '\0';

    parse_form_value(body, "ssid", ssid, sizeof(ssid));
    parse_form_value(body, "password", password, sizeof(password));
    trim_ascii_whitespace(ssid);
    trim_ascii_whitespace(password);

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_FAIL;
    }

    esp_err_t err = nvs_save_wifi_credentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save credentials failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return err;
    }

    strlcpy(s_sta_ssid, ssid, sizeof(s_sta_ssid));
    strlcpy(s_sta_password, password, sizeof(s_sta_password));
    s_sta_configured = true;
    s_retry_num = 0;

    ESP_LOGI(TAG, "saved Wi-Fi config for SSID: %s", s_sta_ssid);
    err = wifi_portal_connect_sta();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "manual STA connect request failed: %s", esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(
        req,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head><body>"
        "<p>已保存并开始连接路由器。</p>"
        "<p><a href=\"/\">返回状态页</a></p>"
        "</body></html>");
}

/* 启动 HTTP 服务器并注册 3 个入口：主页、状态页、提交页。 */
static esp_err_t start_webserver(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = WIFI_HTTPD_STACK_SIZE;
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = wifi_index_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = wifi_status_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t configure_uri = {
        .uri = "/configure",
        .method = HTTP_POST,
        .handler = wifi_configure_handler,
        .user_ctx = NULL,
    };

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_register_uri_handler(s_server, &index_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &favicon_uri);
    httpd_register_uri_handler(s_server, &configure_uri);
    return ESP_OK;
}

/* STA 连接核心逻辑：
 * - 正常首次连接：直接 connect()
 * - 已在连/已连接：先 disconnect()，等事件回调后再 connect()
 */
static esp_err_t wifi_portal_connect_sta(void)
{
    wifi_config_t sta_cfg = { 0 };
    bool was_active = s_sta_connected || s_sta_connecting;

    if (!s_sta_configured) {
        ESP_LOGW(TAG, "STA credentials not configured yet");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_wifi_stack_ready) {
        ESP_LOGW(TAG, "Wi-Fi stack is not ready, skip STA connect");
        return ESP_ERR_INVALID_STATE;
    }

    strlcpy((char *)sta_cfg.sta.ssid, s_sta_ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, s_sta_password, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_cfg.sta.failure_retry_cnt = WIFI_MAXIMUM_RETRY;
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(err));
        return err;
    }

    s_sta_connected = false;
    strlcpy(s_sta_ip, "0.0.0.0", sizeof(s_sta_ip));

    if (was_active) {
        s_reconnect_requested = true;
        err = esp_wifi_disconnect();
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if (err != ESP_ERR_WIFI_NOT_CONNECT && err != ESP_ERR_WIFI_STATE) {
            ESP_LOGW(TAG, "esp_wifi_disconnect before reconnect: %s", esp_err_to_name(err));
            return err;
        }
        s_reconnect_requested = false;
    }

    s_sta_connecting = true;
    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_CONN) {
        s_sta_connecting = true;
        return err;
    }
    if (err != ESP_OK) {
        s_sta_connecting = false;
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

/* Wi-Fi / IP 事件统一入口。
 * 这里只维护状态机，不做会阻塞主语音链路的重操作。
 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (s_sta_configured) {
                esp_err_t err = wifi_portal_connect_sta();
                if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                    ESP_LOGW(TAG, "initial STA connect skipped: %s", esp_err_to_name(err));
                }
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_sta_connected = false;
            s_sta_connecting = false;
            strlcpy(s_sta_ip, "0.0.0.0", sizeof(s_sta_ip));

            if (s_reconnect_requested) {
                s_reconnect_requested = false;
                s_sta_connecting = true;
                esp_err_t err = esp_wifi_connect();
                if (err == ESP_OK || err == ESP_ERR_WIFI_CONN) {
                    break;
                }
                s_sta_connecting = false;
                ESP_LOGW(TAG, "reconnect after config change failed: %s", esp_err_to_name(err));
                break;
            }

            if (s_sta_configured && s_retry_num < WIFI_MAXIMUM_RETRY) {
                s_retry_num++;
                ESP_LOGW(TAG, "STA disconnected, retry %d/%d", s_retry_num, WIFI_MAXIMUM_RETRY);
                s_sta_connecting = true;
                esp_err_t err = esp_wifi_connect();
                if (err == ESP_OK || err == ESP_ERR_WIFI_CONN) {
                    break;
                }
                s_sta_connecting = false;
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "retry esp_wifi_connect failed: %s", esp_err_to_name(err));
                }
            } else if (s_sta_configured) {
                ESP_LOGW(TAG, "STA connect failed, keep AP config page at http://%s", WIFI_PORTAL_AP_IP);
            }
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "config AP ready: SSID=%s password=%s url=http://%s",
                     WIFI_PORTAL_AP_SSID, WIFI_PORTAL_AP_PASSWORD, WIFI_PORTAL_AP_IP);
            break;
        default:
            break;
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_num = 0;
        s_sta_connected = true;
        s_sta_connecting = false;
        s_reconnect_requested = false;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA connected to %s, ip=%s", s_sta_ssid, s_sta_ip);
    }
}

/* 固定 SoftAP 地址为 192.168.4.1，便于用户记忆和浏览器访问。 */
static esp_err_t configure_softap_network(void)
{
    esp_netif_ip_info_t ip_info = { 0 };
    esp_err_t err;

    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return err;
    }
    return ESP_OK;
}

/* 模块总入口：
 * 1. 初始化 NVS / netif / event loop
 * 2. 创建 AP/STA 双模网络
 * 3. 读取保存的 Wi-Fi 配置
 * 4. 启动 AP 和配置网页
 * 5. 若已保存凭据，则自动尝试 STA 联网
 */
esp_err_t wifi_portal_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "failed to create default Wi-Fi netif");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_wifi_stack_ready = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register WIFI_EVENT handler failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register IP_EVENT handler failed: %s", esp_err_to_name(err));
        return err;
    }

    err = configure_softap_network();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configure_softap_network failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t ap_cfg = { 0 };
    strlcpy((char *)ap_cfg.ap.ssid, WIFI_PORTAL_AP_SSID, sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, WIFI_PORTAL_AP_PASSWORD, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len = strlen(WIFI_PORTAL_AP_SSID);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    err = nvs_load_wifi_credentials();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "loaded saved Wi-Fi credentials for SSID: %s", s_sta_ssid);
    } else {
        s_sta_ssid[0] = '\0';
        s_sta_password[0] = '\0';
        s_sta_configured = false;
        if (!apply_default_sta_credentials()) {
            ESP_LOGI(TAG, "no saved Wi-Fi credentials yet; use config AP to set them");
        }
    }

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(APSTA) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    err = start_webserver();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start_webserver failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_sta_configured) {
        ESP_LOGI(TAG, "connect to AP '%s' and open http://%s",
                 WIFI_PORTAL_AP_SSID, WIFI_PORTAL_AP_IP);
    }

    return ESP_OK;
}

/* ────────────────────── 对外状态查询接口 ────────────────────── */

bool wifi_portal_is_sta_connected(void)
{
    return s_sta_connected;
}

const char *wifi_portal_get_sta_ip(void)
{
    return s_sta_ip;
}

const char *wifi_portal_get_sta_ssid(void)
{
    return s_sta_ssid;
}
