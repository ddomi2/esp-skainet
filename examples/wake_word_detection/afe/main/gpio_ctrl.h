/**
 * @file gpio_ctrl.h
 * @brief GPIO 设备控制模块 — 语音命令执行硬件动作
 *
 * 本模块负责：
 *   - 初始化 GPIO 引脚（输出模式）
 *   - 提供开灯/关灯、开关风扇等硬件控制接口
 *
 * 接线说明（ESP32-S3-DevKitC-1）：
 *   LED  正极 → 330Ω 电阻 → GPIO 2
 *   LED  负极 → GND
 *   风扇 信号 → GPIO 7（通过继电器模块或直驱小风扇）
 *   风扇 GND  → GND
 *
 * 已占用引脚（不可使用）：
 *   GPIO 4  — I2S SCK (INMP441)
 *   GPIO 5  — I2S WS  (INMP441)
 *   GPIO 6  — I2S SD  (INMP441)
 *   GPIO 26~37 — Flash/PSRAM (N16R8)
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════
 *  GPIO 引脚定义
 *  如需更换引脚，只需修改此处的宏定义
 * ═══════════════════════════════════════════════════════════ */
#define GPIO_LED_PIN    2   /* LED 灯控制引脚 */
#define GPIO_FAN_PIN    7   /* 风扇控制引脚（继电器或直驱） */

/**
 * @brief 初始化所有 GPIO 引脚（在 app_main 中调用一次）
 *
 * @return ESP_OK 初始化成功，否则返回错误码
 */
esp_err_t gpio_ctrl_init(void);

/**
 * @brief 控制 LED 开关
 *
 * @param on  true=开灯, false=关灯
 */
void gpio_ctrl_led_set(bool on);

/**
 * @brief 获取 LED 当前状态
 *
 * @return true=灯亮, false=灯灭
 */
bool gpio_ctrl_led_get(void);

/**
 * @brief 切换 LED 状态（亮→灭，灭→亮）
 */
void gpio_ctrl_led_toggle(void);

/**
 * @brief 控制风扇开关
 *
 * @param on  true=开启, false=关闭
 */
void gpio_ctrl_fan_set(bool on);

/**
 * @brief 获取风扇当前状态
 *
 * @return true=运行中, false=已关闭
 */
bool gpio_ctrl_fan_get(void);
