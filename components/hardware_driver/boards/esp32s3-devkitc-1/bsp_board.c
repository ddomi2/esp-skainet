/**
 *
 * @copyright Copyright 2021 Espressif Systems (Shanghai) Co. Ltd.
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *               http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "string.h"
#include "bsp_board.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "soc/soc_caps.h"
#else
#include "driver/i2s.h"
#endif
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "devkitc1_board";

/**
 * AFE 需要 2 通道输入：1 路麦克风 + 1 路空参考（用于回声消除占位）
 */
#define ADC_I2S_CHANNEL 2

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static i2s_chan_handle_t rx_handle = NULL;
#endif

/**
 * @brief 初始化 I2S 外设（仅接收模式，用于 INMP441 麦克风）
 *
 * @param i2s_num   I2S 端口号
 * @param sample_rate 采样率（唤醒词检测固定使用 16000）
 */
static esp_err_t bsp_i2s_init(i2s_port_t i2s_num, uint32_t sample_rate)
{
    esp_err_t ret_val = ESP_OK;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    /* 创建 I2S 接收通道 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_num, I2S_ROLE_MASTER);
    ret_val |= i2s_new_channel(&chan_cfg, NULL, &rx_handle);

    /*
     * INMP441 配置要点：
     * - 32-bit 帧宽（INMP441 在 32-bit 帧中左对齐输出 24-bit 数据）
     * - 单声道模式，通过 slot_mask 选择 L 或 R 声道
     * - 无需 MCLK（INMP441 内部自带时钟恢复电路）
     */
    i2s_std_config_t std_cfg = I2S_CONFIG_DEFAULT(sample_rate, I2S_SLOT_MODE_MONO, 32);
    std_cfg.slot_cfg.slot_mask = INMP441_I2S_SLOT;
    ret_val |= i2s_channel_init_std_mode(rx_handle, &std_cfg);
    ret_val |= i2s_channel_enable(rx_handle);
#else
    i2s_config_t i2s_config = I2S_CONFIG_DEFAULT(sample_rate, I2S_CHANNEL_FMT_ONLY_LEFT, 32);

    i2s_pin_config_t pin_config = {
        .bck_io_num = GPIO_I2S_SCLK,
        .ws_io_num = GPIO_I2S_LRCK,
        .data_out_num = GPIO_I2S_DOUT,
        .data_in_num = GPIO_I2S_SDIN,
        .mck_io_num = GPIO_I2S_MCLK,
    };

    ret_val |= i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
    ret_val |= i2s_set_pin(i2s_num, &pin_config);
#endif

    return ret_val;
}

static esp_err_t bsp_i2s_deinit(i2s_port_t i2s_num)
{
    esp_err_t ret_val = ESP_OK;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ret_val |= i2s_channel_disable(rx_handle);
    ret_val |= i2s_del_channel(rx_handle);
    rx_handle = NULL;
#else
    ret_val |= i2s_stop(i2s_num);
    ret_val |= i2s_driver_uninstall(i2s_num);
#endif

    return ret_val;
}

/**
 * @brief 从 INMP441 读取音频数据并转换为 AFE 所需格式
 *
 * 数据流：
 *   INMP441 → I2S (32-bit) → 右移取高16位 → 交织为 [mic, 0] 双通道 → AFE
 *
 * 内存布局（原地转换，无额外分配）：
 *   输入: | int32[0] | int32[1] | ... | int32[N-1] |   (N 个 32-bit 采样)
 *   输出: | int16 mic[0] | int16 ref[0] | ... |        (N 对 16-bit 采样)
 *   输入输出字节数相同 (N*4)，从末尾向前处理避免覆盖
 */
esp_err_t bsp_get_feed_data(bool is_get_raw_channel, int16_t *buffer, int buffer_len)
{
    esp_err_t ret = ESP_OK;
    size_t bytes_read = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ret = i2s_channel_read(rx_handle, buffer, buffer_len, &bytes_read, portMAX_DELAY);
#else
    ret = i2s_read(I2S_NUM_1, buffer, buffer_len, &bytes_read, portMAX_DELAY);
#endif

    /* 根据实际读到的字节数计算有效采样数（防止处理未初始化数据） */
    int actual_samples = bytes_read / sizeof(int32_t);

    int32_t *raw = (int32_t *)buffer;
    int16_t *out = buffer;

    /* 从末尾向前遍历：因为输出写入位置 [i*2, i*2+1] 会覆盖 raw[i] 的内存 */
    for (int i = actual_samples - 1; i >= 0; i--) {
        int16_t sample = (int16_t)(raw[i] >> INMP441_BIT_SHIFT);
        out[i * 2 + 0] = sample;  /* 麦克风通道 */
        out[i * 2 + 1] = 0;       /* 空参考通道（AFE 回声消除占位） */
    }

    /* 如果实际读取不足，将剩余部分填零（避免 AFE 处理噪声数据） */
    int expected_samples = buffer_len / sizeof(int32_t);
    if (actual_samples < expected_samples) {
        memset(&out[actual_samples * 2], 0,
               (expected_samples - actual_samples) * 2 * sizeof(int16_t));
    }

    return ret;
}

int bsp_get_feed_channel(void)
{
    return ADC_I2S_CHANNEL;
}

/**
 * @brief 返回 AFE 输入格式标识
 *
 * "MN" = 1 Mic + 1 Null reference
 * AFE 据此决定内部处理通道数和回声消除策略
 */
char* bsp_get_input_format(void)
{
    return "MN";
}

esp_err_t bsp_board_init(uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    ESP_LOGI(TAG, "初始化 ESP32-S3-DevKitC-1 + INMP441");
    ESP_LOGI(TAG, "I2S 引脚 — SCK: %d, WS: %d, SD: %d",
             GPIO_I2S_SCLK, GPIO_I2S_LRCK, GPIO_I2S_SDIN);
    ESP_LOGI(TAG, "采样率: %lu Hz, 位移: %d", (unsigned long)sample_rate, INMP441_BIT_SHIFT);

    /* 使用调用者传入的采样率（唤醒词场景固定 16000） */
    esp_err_t ret = bsp_i2s_init(I2S_NUM_1, sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 初始化失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bsp_audio_play(const int16_t* data, int length, TickType_t ticks_to_wait)
{
    /* 本板级仅有麦克风输入，无 DAC/功放，不支持播放 */
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_audio_set_play_vol(int volume)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_audio_get_play_vol(int *volume)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sdcard_init(char *mount_point, size_t max_files)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sdcard_deinit(char *mount_point)
{
    return ESP_ERR_NOT_SUPPORTED;
}
