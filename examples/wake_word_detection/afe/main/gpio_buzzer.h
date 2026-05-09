/**
 * @file gpio_buzzer.h
 * @brief 蜂鸣器控制接口（PWM 音量调节）
 *
 * 接线：GPIO 9 → 蜂鸣器(+) → GND
 * 原理：使用 LEDC 外设输出 2kHz PWM，占空比控制音量
 * 音量等级：1~10（对应占空比 10%~100%）
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define GPIO_BUZZER_PIN  9   /* 蜂鸣器 PWM 引脚 */

/** 初始化蜂鸣器 LEDC PWM */
esp_err_t gpio_buzzer_init(void);

/** 开/关蜂鸣器（开启时以当前音量发声） */
void gpio_buzzer_set(bool on);

/** 获取蜂鸣器开关状态 */
bool gpio_buzzer_get(void);

/** 音量 +1（最大 10） */
void gpio_buzzer_vol_up(void);

/** 音量 -1（最小 1） */
void gpio_buzzer_vol_down(void);

/** 获取当前音量等级 (1~10) */
int gpio_buzzer_get_vol(void);
