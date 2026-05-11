/**
 * @file gpio_servo.h
 * @brief 舵机控制接口 — PWM 角度控制
 *
 * 接线：GPIO 16 → 舵机信号线（橙色/白色）
 *       5V     → 舵机电源（红色）— 建议外部供电
 *       GND    → 舵机地线（棕色/黑色）
 *
 * 原理：
 *   标准舵机使用 50Hz PWM，脉宽决定角度：
 *   - 0.5ms (2.5% duty) → 0°
 *   - 1.5ms (7.5% duty) → 90°（居中）
 *   - 2.5ms (12.5% duty) → 180°
 */
#pragma once

#include "esp_err.h"

#define GPIO_SERVO_PIN  16   /* 舵机 PWM 信号引脚 */

/** 初始化舵机 PWM（50Hz） */
esp_err_t gpio_servo_init(void);

/** 设置舵机角度 (0~180°) */
void gpio_servo_set_angle(int angle);

/** 舵机回到居中位置 (90°) */
void gpio_servo_center(void);

/** 获取当前角度 */
int gpio_servo_get_angle(void);
