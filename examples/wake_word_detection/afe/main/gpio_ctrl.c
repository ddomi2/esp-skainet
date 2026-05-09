/**
 * @file gpio_ctrl.c
 * @brief GPIO 设备控制实现 — LED + 风扇 + 蜂鸣器(PWM)
 *
 * LED/风扇使用简单 GPIO 高低电平控制。
 * 蜂鸣器使用 LEDC (PWM) 驱动，可调节音量（占空比）。
 */
#include "gpio_ctrl.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "gpio_ctrl";

/* ── 设备状态 ── */
static bool s_led_state = false;
static bool s_fan_state = false;
static bool s_buzzer_state = false;
static int  s_buzzer_vol = 5;    /* 音量等级 1~10，默认 5（50% 占空比） */

/* ── LEDC PWM 配置常量 ── */
#define BUZZER_LEDC_TIMER     LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BUZZER_FREQ_HZ        2000   /* 蜂鸣器频率 2kHz（清脆的"嘟"声） */
#define BUZZER_DUTY_RES       LEDC_TIMER_10_BIT  /* 10位分辨率 (0~1023) */
#define BUZZER_MAX_DUTY       1023

/**
 * @brief 初始化所有 GPIO 引脚 + 蜂鸣器 PWM
 */
esp_err_t gpio_ctrl_init(void)
{
    /* ── LED + 风扇引脚配置（简单 GPIO 输出）── */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED_PIN) | (1ULL << GPIO_FAN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(GPIO_LED_PIN, 0);
    gpio_set_level(GPIO_FAN_PIN, 0);

    /* ── 蜂鸣器 LEDC PWM 配置 ──
     * 使用 LEDC 定时器产生固定频率方波，通过调节占空比控制音量
     */
    ledc_timer_config_t timer_conf = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .timer_num       = BUZZER_LEDC_TIMER,
        .duty_resolution = BUZZER_DUTY_RES,      /* 10 位分辨率 */
        .freq_hz         = BUZZER_FREQ_HZ,       /* 2000 Hz */
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ch_conf = {
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .gpio_num   = GPIO_BUZZER_PIN,
        .duty       = 0,                 /* 初始占空比 = 0（关闭） */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_led_state = false;
    s_fan_state = false;
    s_buzzer_state = false;
    s_buzzer_vol = 5;

    ESP_LOGI(TAG, "GPIO 初始化完成 — LED: GPIO%d, 风扇: GPIO%d, 蜂鸣器: GPIO%d (PWM %dHz)",
             GPIO_LED_PIN, GPIO_FAN_PIN, GPIO_BUZZER_PIN, BUZZER_FREQ_HZ);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════
 *  LED 控制
 * ═══════════════════════════════════════════════════════════ */

void gpio_ctrl_led_set(bool on)
{
    s_led_state = on;
    gpio_set_level(GPIO_LED_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "LED %s (GPIO%d)", on ? "ON 💡" : "OFF ⚫", GPIO_LED_PIN);
}

bool gpio_ctrl_led_get(void)
{
    return s_led_state;
}

void gpio_ctrl_led_toggle(void)
{
    gpio_ctrl_led_set(!s_led_state);
}

/* ═══════════════════════════════════════════════════════════
 *  风扇控制
 * ═══════════════════════════════════════════════════════════ */

void gpio_ctrl_fan_set(bool on)
{
    s_fan_state = on;
    gpio_set_level(GPIO_FAN_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "风扇 %s (GPIO%d)", on ? "ON 🌀" : "OFF ⚫", GPIO_FAN_PIN);
}

bool gpio_ctrl_fan_get(void)
{
    return s_fan_state;
}

/* ═══════════════════════════════════════════════════════════
 *  蜂鸣器控制（PWM 音量调节）
 *
 *  原理：
 *    - LEDC 以 2kHz 频率输出方波到蜂鸣器
 *    - 占空比越高 → 有效功率越大 → 声音越大
 *    - 占空比 = 0 → 蜂鸣器静音
 *    - 音量等级 1~10 → 对应占空比 10%~100%
 * ═══════════════════════════════════════════════════════════ */

/**
 * @brief 内部函数：根据当前音量等级设置 PWM 占空比
 */
static void buzzer_update_duty(void)
{
    if (!s_buzzer_state) {
        /* 蜂鸣器关闭状态，占空比设为 0 */
        ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    } else {
        /* 音量等级 1~10 → 占空比 10%~100% */
        uint32_t duty = (BUZZER_MAX_DUTY * s_buzzer_vol) / 10;
        ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
    }
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void gpio_ctrl_buzzer_set(bool on)
{
    s_buzzer_state = on;
    buzzer_update_duty();
    ESP_LOGI(TAG, "蜂鸣器 %s (GPIO%d, 音量=%d/10)",
             on ? "ON 🔔" : "OFF 🔇", GPIO_BUZZER_PIN, s_buzzer_vol);
}

bool gpio_ctrl_buzzer_get(void)
{
    return s_buzzer_state;
}

void gpio_ctrl_buzzer_vol_up(void)
{
    if (s_buzzer_vol < 10) {
        s_buzzer_vol++;
    }
    /* 如果蜂鸣器正在响，立即更新音量 */
    if (s_buzzer_state) {
        buzzer_update_duty();
    }
    ESP_LOGI(TAG, "🔊 蜂鸣器音量: %d/10", s_buzzer_vol);
}

void gpio_ctrl_buzzer_vol_down(void)
{
    if (s_buzzer_vol > 1) {
        s_buzzer_vol--;
    }
    if (s_buzzer_state) {
        buzzer_update_duty();
    }
    ESP_LOGI(TAG, "🔉 蜂鸣器音量: %d/10", s_buzzer_vol);
}

int gpio_ctrl_buzzer_get_vol(void)
{
    return s_buzzer_vol;
}
