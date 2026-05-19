#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file wifi_portal.h
 * @brief Wi-Fi 配置门户接口
 *
 * 本模块提供一个“始终可进入”的联网入口：
 * 1. 设备启动后开启固定 SoftAP
 * 2. 用户通过浏览器访问固定地址填写路由器 SSID/密码
 * 3. 设备将配置写入 NVS，并尝试以 STA 模式联网
 *
 * 设计目标：
 * - 即使 Wi-Fi 未配置、配置错误或联网失败，也不影响当前离线语音功能
 * - 配置 AP 始终保留，便于后续重新改网
 */

/** 固定配置 AP 名称。用户连接该热点进入配置页。 */
#define WIFI_PORTAL_AP_SSID "Skainet-Setup"
/** 固定配置 AP 密码。 */
#define WIFI_PORTAL_AP_PASSWORD "skainet8"
/** 固定配置页地址。 */
#define WIFI_PORTAL_AP_IP "192.168.4.1"

/**
 * @brief 初始化 Wi-Fi 配置门户
 *
 * 初始化内容包括：
 * - NVS
 * - esp_netif / event loop
 * - SoftAP + STA 双模
 * - 内置 HTTP 配置页面
 * - 读取并尝试使用已保存的 Wi-Fi 凭据
 *
 * @return
 *   - ESP_OK: 配置门户已启动
 *   - 其他错误码: Wi-Fi 相关初始化失败
 */
esp_err_t wifi_portal_init(void);

/** @brief 查询 STA 是否已成功连上路由器。 */
bool wifi_portal_is_sta_connected(void);

/** @brief 获取当前 STA IP；未联网时返回 "0.0.0.0"。 */
const char *wifi_portal_get_sta_ip(void);

/** @brief 获取当前保存的 STA SSID；未配置时返回空串。 */
const char *wifi_portal_get_sta_ssid(void);

#ifdef __cplusplus
}
#endif
