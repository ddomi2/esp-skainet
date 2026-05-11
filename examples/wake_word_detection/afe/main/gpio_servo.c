/**
 * @file gpio_servo.c
 * @brief 舵机控制实现 — 50Hz PWM 角度控制
 *
 * 原理：
 *   舵机通过 PWM 脉冲宽度确定目标角度：
 *   - 周期 = 20ms (50Hz)
 *   - 脉宽 0.5ms → 0°（最左）
 *   - 脉宽 1.5ms → 90°（居中）
 *   - 脉宽 2.5ms → 180°（最右）
 *
 *   使用 14 位分辨率 (0~16383)：
 *   - 0.5ms / 20ms * 16383 ≈ 409  → 0°
 *   - 2.5ms / 20ms * 16383 ≈ 2047 → 180°
 */
#include "gpio_servo.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "gpio_servo";

/* ── LEDC PWM 配置 ── */
#define SERVO_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER     LEDC_TIMER_2       /* 用 TIMER_2，避免冲突 */
#define SERVO_LEDC_CHANNEL   LEDC_CHANNEL_3     /* 用 CHANNEL_3 */
#define SERVO_FREQ_HZ        50                 /* 舵机标准频率 50Hz */
#define SERVO_DUTY_RES       LEDC_TIMER_14_BIT  /* 14位分辨率 (0~16383) */
#define SERVO_MAX_DUTY       16383

/* 脉宽范围对应的 duty 值（14位分辨率下） */
#define SERVO_DUTY_MIN       409    /* 0.5ms / 20ms * 16383 ≈ 409  → 0° */
#define SERVO_DUTY_MAX       2047   /* 2.5ms / 20ms * 16383 ≈ 2047 → 180° */

static int s_angle = 90;  /* 默认居中 */

/**
 * @brief 内部：角度转换为 LEDC duty 值
 */
static uint32_t angle_to_duty(int angle)
{
    /* 线性映射: 0°→DUTY_MIN, 180°→DUTY_MAX */
    return SERVO_DUTY_MIN + (SERVO_DUTY_MAX - SERVO_DUTY_MIN) * angle / 180;
}

esp_err_t gpio_servo_init(void)
{
    /* 配置 LEDC 定时器 — 50Hz */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = SERVO_LEDC_MODE,
        .timer_num       = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_DUTY_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "舵机 LEDC 定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置 LEDC 通道 */
    ledc_channel_config_t ch_cfg = {
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_LEDC_CHANNEL,
        .timer_sel  = SERVO_LEDC_TIMER,
        .gpio_num   = GPIO_SERVO_PIN,
        .duty       = angle_to_duty(90),  /* 初始居中 */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "舵机 LEDC 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_angle = 90;
    ESP_LOGI(TAG, "舵机初始化完成 (GPIO%d, 50Hz, 居中90°)", GPIO_SERVO_PIN);
    return ESP_OK;
}

void gpio_servo_set_angle(int angle)
{
    /* 限制范围 0~180 */
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    s_angle = angle;
    uint32_t duty = angle_to_duty(angle);
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    ESP_LOGI(TAG, "🎯 舵机角度: %d°", angle);
}

void gpio_servo_center(void)
{
    gpio_servo_set_angle(90);
}

int gpio_servo_get_angle(void)
{
    return s_angle;
}
