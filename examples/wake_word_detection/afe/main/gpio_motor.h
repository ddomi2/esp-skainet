/**
 * @file gpio_motor.h
 * @brief L298N 双电机驱动接口 — 遥控车前进/后退/转向/停止
 *
 * 接线（L298N 模块 → ESP32-S3）：
 *   ENA → GPIO 10 (PWM，左电机速度)
 *   IN1 → GPIO 11 (左电机方向A)
 *   IN2 → GPIO 12 (左电机方向B)
 *   ENB → GPIO 13 (PWM，右电机速度)
 *   IN3 → GPIO 14 (右电机方向A)
 *   IN4 → GPIO 15 (右电机方向B)
 *
 * L298N 模块供电：
 *   12V → 电池正极（驱动电机）
 *   GND → 电池负极 + ESP32 GND（共地！）
 *   5V  → 可给 ESP32 供电（板载稳压）
 *
 * 方向逻辑（以左电机为例）：
 *   IN1=HIGH, IN2=LOW  → 正转（前进）
 *   IN1=LOW,  IN2=HIGH → 反转（后退）
 *   IN1=LOW,  IN2=LOW  → 停止（滑行）
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* ── L298N 引脚定义 ── */
#define MOTOR_ENA_PIN  10   /* 左电机使能 (PWM 调速) */
#define MOTOR_IN1_PIN  11   /* 左电机方向 A */
#define MOTOR_IN2_PIN  12   /* 左电机方向 B */
#define MOTOR_ENB_PIN  13   /* 右电机使能 (PWM 调速) */
#define MOTOR_IN3_PIN  14   /* 右电机方向 A */
#define MOTOR_IN4_PIN  15   /* 右电机方向 B */

/* ── 运动方向枚举 ── */
typedef enum {
    MOTOR_STOP = 0,   /* 停止 */
    MOTOR_FORWARD,    /* 前进 */
    MOTOR_BACKWARD,   /* 后退 */
    MOTOR_LEFT,       /* 左转（左轮停/右轮转） */
    MOTOR_RIGHT,      /* 右转（左轮转/右轮停） */
} motor_dir_t;

/** 初始化 L298N 所有引脚（GPIO + PWM） */
esp_err_t gpio_motor_init(void);

/** 设置运动方向 */
void gpio_motor_move(motor_dir_t dir);

/** 设置电机速度 (0~100，对应 PWM 占空比百分比) */
void gpio_motor_set_speed(int percent);

/** 获取当前方向 */
motor_dir_t gpio_motor_get_dir(void);

/** 获取当前速度 */
int gpio_motor_get_speed(void);
