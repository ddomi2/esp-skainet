/**
 * @file gpio_fan.h
 * @brief 风扇控制接口
 *
 * 接线：GPIO 7 → 继电器 IN 或直驱 3.3V 小风扇
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define GPIO_FAN_PIN  7   /* 风扇控制引脚 */

/** 初始化风扇引脚 */
esp_err_t gpio_fan_init(void);

/** 开/关风扇 */
void gpio_fan_set(bool on);

/** 获取当前状态 */
bool gpio_fan_get(void);
