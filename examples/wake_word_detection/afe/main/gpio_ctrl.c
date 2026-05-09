/**
 * @file gpio_ctrl.c
 * @brief GPIO 总控 — 统一初始化所有设备
 *
 * 各设备的实际实现分布在独立文件中：
 *   gpio_led.c / gpio_fan.c / gpio_buzzer.c
 * 本文件只做"统一入口"，依次调用各子模块的 init。
 */
#include "gpio_ctrl.h"
#include "esp_log.h"

static const char *TAG = "gpio_ctrl";

esp_err_t gpio_ctrl_init(void)
{
    esp_err_t ret;

    /* 1. 初始化 LED */
    ret = gpio_led_init();
    if (ret != ESP_OK) return ret;

    /* 2. 初始化风扇 */
    ret = gpio_fan_init();
    if (ret != ESP_OK) return ret;

    /* 3. 初始化蜂鸣器 (PWM) */
    ret = gpio_buzzer_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "所有 GPIO 设备初始化完成 ✅");
    return ESP_OK;
}

