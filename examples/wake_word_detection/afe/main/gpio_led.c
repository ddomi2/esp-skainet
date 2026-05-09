/**
 * @file gpio_led.c
 * @brief LED 灯控制实现 — 简单 GPIO 高低电平
 */
#include "gpio_led.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_led";
static bool s_state = false;

esp_err_t gpio_led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED 引脚配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(GPIO_LED_PIN, 0);
    s_state = false;
    ESP_LOGI(TAG, "LED 初始化完成 (GPIO%d)", GPIO_LED_PIN);
    return ESP_OK;
}

void gpio_led_set(bool on)
{
    s_state = on;
    gpio_set_level(GPIO_LED_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "LED %s", on ? "ON 💡" : "OFF ⚫");
}

bool gpio_led_get(void)
{
    return s_state;
}

void gpio_led_toggle(void)
{
    gpio_led_set(!s_state);
}
