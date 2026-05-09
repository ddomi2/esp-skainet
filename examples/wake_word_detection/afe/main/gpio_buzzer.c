/**
 * @file gpio_buzzer.c
 * @brief 蜂鸣器控制实现 — LEDC PWM 驱动，可调节音量
 *
 * 原理：
 *   - LEDC 以 2kHz 频率输出方波 → 蜂鸣器发出"嘟"声
 *   - 占空比越高 → 有效功率越大 → 声音越大
 *   - 音量等级 1~10 → 对应占空比 10%~100%
 */
#include "gpio_buzzer.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "gpio_buzzer";

/* ── LEDC 配置常量 ── */
#define BUZZER_LEDC_TIMER     LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BUZZER_FREQ_HZ        2000              /* 蜂鸣器频率 2kHz */
#define BUZZER_DUTY_RES       LEDC_TIMER_10_BIT /* 10位分辨率 (0~1023) */
#define BUZZER_MAX_DUTY       1023

/* ── 状态变量 ── */
static bool s_state = false;
static int  s_vol   = 5;   /* 默认音量等级 5（50% 占空比） */

/**
 * @brief 内部：根据当前状态和音量更新 PWM 占空比
 */
static void buzzer_update_duty(void)
{
    uint32_t duty = 0;
    if (s_state) {
        /* 音量 1~10 → 占空比 10%~100% */
        duty = (BUZZER_MAX_DUTY * s_vol) / 10;
    }
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

esp_err_t gpio_buzzer_init(void)
{
    /* 配置 LEDC 定时器 — 控制 PWM 频率 */
    ledc_timer_config_t timer_conf = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .timer_num       = BUZZER_LEDC_TIMER,
        .duty_resolution = BUZZER_DUTY_RES,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置 LEDC 通道 — 绑定 GPIO + 控制占空比 */
    ledc_channel_config_t ch_conf = {
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .gpio_num   = GPIO_BUZZER_PIN,
        .duty       = 0,       /* 初始关闭 */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_state = false;
    s_vol = 5;
    ESP_LOGI(TAG, "蜂鸣器初始化完成 (GPIO%d, PWM %dHz)", GPIO_BUZZER_PIN, BUZZER_FREQ_HZ);
    return ESP_OK;
}

void gpio_buzzer_set(bool on)
{
    s_state = on;
    buzzer_update_duty();
    ESP_LOGI(TAG, "蜂鸣器 %s (音量=%d/10)", on ? "ON 🔔" : "OFF 🔇", s_vol);
}

bool gpio_buzzer_get(void)
{
    return s_state;
}

void gpio_buzzer_vol_up(void)
{
    if (s_vol < 10) s_vol++;
    if (s_state) buzzer_update_duty();  /* 正在响时立即生效 */
    ESP_LOGI(TAG, "🔊 音量: %d/10", s_vol);
}

void gpio_buzzer_vol_down(void)
{
    if (s_vol > 1) s_vol--;
    if (s_state) buzzer_update_duty();
    ESP_LOGI(TAG, "🔉 音量: %d/10", s_vol);
}

int gpio_buzzer_get_vol(void)
{
    return s_vol;
}
