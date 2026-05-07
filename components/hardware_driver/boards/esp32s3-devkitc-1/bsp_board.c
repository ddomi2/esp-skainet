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

/* AFE expects 2-channel feed: mic + null reference */
#define ADC_I2S_CHANNEL 2

static int s_play_sample_rate = 16000;
static int s_play_channel_format = 1;
static int s_bits_per_chan = 16;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static i2s_chan_handle_t rx_handle = NULL;
#endif

static esp_err_t bsp_i2s_init(i2s_port_t i2s_num, uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    esp_err_t ret_val = ESP_OK;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_num, I2S_ROLE_MASTER);

    ret_val |= i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    i2s_std_config_t std_cfg = I2S_CONFIG_DEFAULT(sample_rate, I2S_SLOT_MODE_MONO, 32);
    std_cfg.slot_cfg.slot_mask = INMP441_I2S_SLOT;
    ret_val |= i2s_channel_init_std_mode(rx_handle, &std_cfg);
    ret_val |= i2s_channel_enable(rx_handle);
#else
    i2s_config_t i2s_config = I2S_CONFIG_DEFAULT(sample_rate, I2S_CHANNEL_FMT_ONLY_LEFT, bits_per_chan);

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

esp_err_t bsp_get_feed_data(bool is_get_raw_channel, int16_t *buffer, int buffer_len)
{
    esp_err_t ret = ESP_OK;
    size_t bytes_read;
    int audio_chunksize = buffer_len / (sizeof(int32_t));

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ret = i2s_channel_read(rx_handle, buffer, buffer_len, &bytes_read, portMAX_DELAY);
#else
    ret = i2s_read(I2S_NUM_1, buffer, buffer_len, &bytes_read, portMAX_DELAY);
#endif

    /*
     * INMP441 outputs 24-bit data left-aligned in 32-bit I2S frames.
     * Convert to interleaved 16-bit {mic, 0} pairs for AFE (2-channel "MN" format).
     * Process from end to avoid overwriting unread data.
     */
    int32_t *raw = (int32_t *)buffer;
    int16_t *out = buffer;
    for (int i = audio_chunksize - 1; i >= 0; i--) {
        int16_t sample = (int16_t)(raw[i] >> INMP441_BIT_SHIFT);
        out[i * 2 + 0] = sample;  // mic channel
        out[i * 2 + 1] = 0;       // null reference channel
    }

    return ret;
}

int bsp_get_feed_channel(void)
{
    return ADC_I2S_CHANNEL;
}

char* bsp_get_input_format(void)
{
    return "MN";
}

esp_err_t bsp_board_init(uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    ESP_LOGI(TAG, "Initializing ESP32-S3-DevKitC-1 with INMP441");
    ESP_LOGI(TAG, "I2S pins — SCK: %d, WS: %d, SD: %d",
             GPIO_I2S_SCLK, GPIO_I2S_LRCK, GPIO_I2S_SDIN);

    esp_err_t ret = bsp_i2s_init(I2S_NUM_1, 16000, 2, 32);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bsp_audio_play(const int16_t* data, int length, TickType_t ticks_to_wait)
{
    ESP_LOGW(TAG, "Audio playback not supported on this board");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_audio_set_play_vol(int volume)
{
    ESP_LOGW(TAG, "Volume control not supported on this board");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_audio_get_play_vol(int *volume)
{
    ESP_LOGW(TAG, "Volume control not supported on this board");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sdcard_init(char *mount_point, size_t max_files)
{
    ESP_LOGW(TAG, "SD card not configured on this board");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sdcard_deinit(char *mount_point)
{
    return ESP_ERR_NOT_SUPPORTED;
}
