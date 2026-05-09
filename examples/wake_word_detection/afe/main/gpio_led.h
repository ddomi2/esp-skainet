/**
 * @file gpio_led.h
 * @brief LED 灯控制接口
 *
 * 接线：GPIO 2 → 330Ω → LED(+) → GND
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define GPIO_LED_PIN  2   /* LED 控制引脚 */

/** 初始化 LED 引脚 */
esp_err_t gpio_led_init(void);

/** 开/关 LED */
void gpio_led_set(bool on);

/** 获取当前状态 */
bool gpio_led_get(void);

/** 切换状态 */
void gpio_led_toggle(void);
