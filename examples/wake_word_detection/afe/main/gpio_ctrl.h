/**
 * @file gpio_ctrl.h
 * @brief GPIO 设备控制模块 — 语音命令执行硬件动作
 *
 * 本模块负责：
 *   - 初始化 GPIO 引脚（输出模式）
 *   - 提供开灯/关灯、开关风扇、蜂鸣器音量控制等硬件接口
 *
 * 接线说明（ESP32-S3-DevKitC-1）：
 *   LED  正极 → 330Ω 电阻 → GPIO 2
 *   LED  负极 → GND
 *   风扇 信号 → GPIO 7（通过继电器模块或直驱小风扇）
 *   风扇 GND  → GND
 *   蜂鸣器 +  → GPIO 9（有源蜂鸣器直连，无源用 PWM 驱动）
 *   蜂鸣器 -  → GND
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
#define GPIO_BUZZER_PIN 9   /* 蜂鸣器控制引脚（PWM 驱动） */

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

/**
 * @brief 控制蜂鸣器开关
 *
 * 开启时以当前音量（PWM 占空比）发声。
 * @param on  true=开启蜂鸣, false=关闭
 */
void gpio_ctrl_buzzer_set(bool on);

/**
 * @brief 获取蜂鸣器当前状态
 *
 * @return true=正在响, false=已关闭
 */
bool gpio_ctrl_buzzer_get(void);

/**
 * @brief 增大蜂鸣器音量（占空比 +10%，最大 100%）
 */
void gpio_ctrl_buzzer_vol_up(void);

/**
 * @brief 减小蜂鸣器音量（占空比 -10%，最小 10%）
 */
void gpio_ctrl_buzzer_vol_down(void);

/**
 * @brief 获取蜂鸣器当前音量等级
 *
 * @return 1~10（对应 10%~100% 占空比）
 */
int gpio_ctrl_buzzer_get_vol(void);
