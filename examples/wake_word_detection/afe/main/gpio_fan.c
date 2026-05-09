/**
 * @file gpio_fan.c
 * @brief 风扇控制实现 — 简单 GPIO 高低电平
 */
#include "gpio_fan.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_fan";
static bool s_state = false;

esp_err_t gpio_fan_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_FAN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "风扇引脚配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(GPIO_FAN_PIN, 0);
    s_state = false;
    ESP_LOGI(TAG, "风扇初始化完成 (GPIO%d)", GPIO_FAN_PIN);
    return ESP_OK;
}

void gpio_fan_set(bool on)
{
    s_state = on;
    gpio_set_level(GPIO_FAN_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "风扇 %s", on ? "ON 🌀" : "OFF ⚫");
}

bool gpio_fan_get(void)
{
    return s_state;
}
