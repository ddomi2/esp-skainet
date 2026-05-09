/**
 * @file gpio_ctrl.c
 * @brief GPIO 设备控制实现 — LED 开关 + 风扇开关
 *
 * 使用 ESP-IDF 的 GPIO 驱动来控制外接设备。
 * 扩展其他设备（继电器、蜂鸣器等）时，在此文件中添加即可。
 */
#include "gpio_ctrl.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_ctrl";

/* 记录各设备当前状态（避免重复读取寄存器） */
static bool s_led_state = false;
static bool s_fan_state = false;

/**
 * @brief 初始化所有 GPIO 引脚
 *
 * 配置为推挽输出模式，初始电平为低（设备关闭）。
 * 后续添加新设备时，在此函数中追加配置即可。
 */
esp_err_t gpio_ctrl_init(void)
{
    /*
     * 同时配置 LED 和风扇引脚
     * pin_bit_mask 使用位或运算，一次配置多个引脚
     */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED_PIN) | (1ULL << GPIO_FAN_PIN),
        .mode         = GPIO_MODE_OUTPUT,         /* 输出模式 */
        .pull_up_en   = GPIO_PULLUP_DISABLE,      /* 不需要上拉 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,    /* 不需要下拉 */
        .intr_type    = GPIO_INTR_DISABLE,        /* 不使用中断 */
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始状态：全部关闭 */
    gpio_set_level(GPIO_LED_PIN, 0);
    gpio_set_level(GPIO_FAN_PIN, 0);
    s_led_state = false;
    s_fan_state = false;

    ESP_LOGI(TAG, "GPIO 初始化完成 — LED: GPIO%d, 风扇: GPIO%d",
             GPIO_LED_PIN, GPIO_FAN_PIN);
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
