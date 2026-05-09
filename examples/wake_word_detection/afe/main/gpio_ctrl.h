/**
 * @file gpio_ctrl.h
 * @brief GPIO 总控头文件 — 统一引入所有设备模块
 *
 * 使用方式：只需 #include "gpio_ctrl.h" 即可访问所有设备接口。
 * 各设备的具体实现在独立文件中：
 *   - gpio_led.h/c    — LED 灯
 *   - gpio_fan.h/c    — 风扇
 *   - gpio_buzzer.h/c — 蜂鸣器 (PWM)
 *
 * 添加新设备时：创建 gpio_xxx.h/c，然后在此处 #include 即可。
 */
#pragma once

#include "esp_err.h"

/* ═══════════════════════════════════════════════════════════
 *  引入各设备模块的头文件
 * ═══════════════════════════════════════════════════════════ */
#include "gpio_led.h"
#include "gpio_fan.h"
#include "gpio_buzzer.h"

/**
 * @brief 初始化所有 GPIO 设备（在 app_main 中调用一次）
 *
 * 内部会依次调用 gpio_led_init() / gpio_fan_init() / gpio_buzzer_init()
 * @return ESP_OK 全部成功，否则返回第一个失败的错误码
 */
esp_err_t gpio_ctrl_init(void);

