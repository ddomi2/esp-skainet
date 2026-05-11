/**
 * @file gpio_motor.c
 * @brief L298N 双电机驱动实现 — PWM 调速 + GPIO 方向控制
 *
 * 原理：
 *   - ENA/ENB 使用 LEDC PWM 控制电机转速（占空比 = 速度百分比）
 *   - IN1~IN4 使用 GPIO 高低电平控制电机旋转方向
 *   - 差速转向：左转时右轮动左轮停，右转时左轮动右轮停
 */
#include "gpio_motor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "gpio_motor";

/* ── LEDC PWM 配置 ── */
#define MOTOR_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_TIMER      LEDC_TIMER_1       /* 用 TIMER_1，避免和蜂鸣器冲突 */
#define MOTOR_LEDC_FREQ_HZ    1000               /* 1kHz PWM（电机驱动常用频率） */
#define MOTOR_LEDC_DUTY_RES   LEDC_TIMER_10_BIT  /* 10位分辨率 (0~1023) */
#define MOTOR_MAX_DUTY        1023
#define MOTOR_CH_LEFT         LEDC_CHANNEL_1     /* 左电机 ENA 通道 */
#define MOTOR_CH_RIGHT        LEDC_CHANNEL_2     /* 右电机 ENB 通道 */

/* ── 状态变量 ── */
static motor_dir_t s_dir   = MOTOR_STOP;
static int         s_speed = 70;  /* 默认速度 70% */

/**
 * @brief 内部：设置单侧电机方向
 * @param in1 方向引脚A
 * @param in2 方向引脚B
 * @param forward true=正转, false=反转
 */
static void set_motor_direction(int in1, int in2, bool forward)
{
    if (forward) {
        gpio_set_level(in1, 1);
        gpio_set_level(in2, 0);
    } else {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 1);
    }
}

/**
 * @brief 内部：停止单侧电机（滑行）
 */
static void stop_motor(int in1, int in2)
{
    gpio_set_level(in1, 0);
    gpio_set_level(in2, 0);
}

/**
 * @brief 内部：更新 PWM 占空比（左右电机）
 */
static void update_pwm(int left_percent, int right_percent)
{
    uint32_t left_duty  = (MOTOR_MAX_DUTY * left_percent) / 100;
    uint32_t right_duty = (MOTOR_MAX_DUTY * right_percent) / 100;

    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_CH_LEFT, left_duty);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_CH_LEFT);

    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_CH_RIGHT, right_duty);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_CH_RIGHT);
}

esp_err_t gpio_motor_init(void)
{
    /* ── 方向引脚配置（IN1~IN4，普通 GPIO 输出）── */
    gpio_config_t dir_cfg = {
        .pin_bit_mask = (1ULL << MOTOR_IN1_PIN) | (1ULL << MOTOR_IN2_PIN) |
                        (1ULL << MOTOR_IN3_PIN) | (1ULL << MOTOR_IN4_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&dir_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "电机方向引脚配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始状态：全部拉低（停止） */
    stop_motor(MOTOR_IN1_PIN, MOTOR_IN2_PIN);
    stop_motor(MOTOR_IN3_PIN, MOTOR_IN4_PIN);

    /* ── 使能引脚 PWM 配置（ENA / ENB）── */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = MOTOR_LEDC_MODE,
        .timer_num       = MOTOR_LEDC_TIMER,
        .duty_resolution = MOTOR_LEDC_DUTY_RES,
        .freq_hz         = MOTOR_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "电机 LEDC 定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 左电机 ENA 通道 */
    ledc_channel_config_t ch_left = {
        .speed_mode = MOTOR_LEDC_MODE,
        .channel    = MOTOR_CH_LEFT,
        .timer_sel  = MOTOR_LEDC_TIMER,
        .gpio_num   = MOTOR_ENA_PIN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_left);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "左电机 PWM 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 右电机 ENB 通道 */
    ledc_channel_config_t ch_right = {
        .speed_mode = MOTOR_LEDC_MODE,
        .channel    = MOTOR_CH_RIGHT,
        .timer_sel  = MOTOR_LEDC_TIMER,
        .gpio_num   = MOTOR_ENB_PIN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_right);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "右电机 PWM 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_dir = MOTOR_STOP;
    s_speed = 70;
    ESP_LOGI(TAG, "L298N 电机驱动初始化完成 (ENA:%d IN1:%d IN2:%d ENB:%d IN3:%d IN4:%d)",
             MOTOR_ENA_PIN, MOTOR_IN1_PIN, MOTOR_IN2_PIN,
             MOTOR_ENB_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN);
    return ESP_OK;
}

void gpio_motor_move(motor_dir_t dir)
{
    s_dir = dir;

    switch (dir) {
    case MOTOR_FORWARD:
        /* 两轮同时正转 */
        set_motor_direction(MOTOR_IN1_PIN, MOTOR_IN2_PIN, true);
        set_motor_direction(MOTOR_IN3_PIN, MOTOR_IN4_PIN, true);
        update_pwm(s_speed, s_speed);
        ESP_LOGI(TAG, "🚗 前进 (速度=%d%%)", s_speed);
        break;

    case MOTOR_BACKWARD:
        /* 两轮同时反转 */
        set_motor_direction(MOTOR_IN1_PIN, MOTOR_IN2_PIN, false);
        set_motor_direction(MOTOR_IN3_PIN, MOTOR_IN4_PIN, false);
        update_pwm(s_speed, s_speed);
        ESP_LOGI(TAG, "🚗 后退 (速度=%d%%)", s_speed);
        break;

    case MOTOR_LEFT:
        /* 差速左转：左轮停，右轮正转 */
        stop_motor(MOTOR_IN1_PIN, MOTOR_IN2_PIN);
        set_motor_direction(MOTOR_IN3_PIN, MOTOR_IN4_PIN, true);
        update_pwm(0, s_speed);
        ESP_LOGI(TAG, "🚗 左转");
        break;

    case MOTOR_RIGHT:
        /* 差速右转：左轮正转，右轮停 */
        set_motor_direction(MOTOR_IN1_PIN, MOTOR_IN2_PIN, true);
        stop_motor(MOTOR_IN3_PIN, MOTOR_IN4_PIN);
        update_pwm(s_speed, 0);
        ESP_LOGI(TAG, "🚗 右转");
        break;

    case MOTOR_STOP:
    default:
        /* 全部停止 */
        stop_motor(MOTOR_IN1_PIN, MOTOR_IN2_PIN);
        stop_motor(MOTOR_IN3_PIN, MOTOR_IN4_PIN);
        update_pwm(0, 0);
        ESP_LOGI(TAG, "🚗 停止");
        break;
    }
}

void gpio_motor_set_speed(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_speed = percent;

    /* 如果正在运动中，立即更新速度 */
    if (s_dir == MOTOR_FORWARD || s_dir == MOTOR_BACKWARD) {
        update_pwm(s_speed, s_speed);
    } else if (s_dir == MOTOR_LEFT) {
        update_pwm(0, s_speed);
    } else if (s_dir == MOTOR_RIGHT) {
        update_pwm(s_speed, 0);
    }
    ESP_LOGI(TAG, "⚡ 速度设为 %d%%", s_speed);
}

motor_dir_t gpio_motor_get_dir(void)
{
    return s_dir;
}

int gpio_motor_get_speed(void)
{
    return s_speed;
}
