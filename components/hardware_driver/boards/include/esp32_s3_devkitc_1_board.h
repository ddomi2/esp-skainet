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
#pragma once

#include "driver/gpio.h"
#include "esp_idf_version.h"

/**
 * @brief ESP32-S3-DevKitC-1 + INMP441 板级配置
 *
 * INMP441 接线（直连 ESP32-S3 GPIO）：
 *   INMP441 SCK  -> GPIO_I2S_SCLK (默认: GPIO 4)
 *   INMP441 WS   -> GPIO_I2S_LRCK (默认: GPIO 5)
 *   INMP441 SD   -> GPIO_I2S_SDIN (默认: GPIO 6)
 *   INMP441 L/R  -> GND (左声道) 或 VDD (右声道)
 *   INMP441 VDD  -> 3.3V
 *   INMP441 GND  -> GND
 *
 * 注意: ESP32-S3 N16R8 (Octal SPI) 的 GPIO 26-37 已被
 *       Flash/PSRAM 占用，不可作为 I2S 引脚使用。
 */

/* ═══════════════════════════════════════════════════════════
 *  I2C 配置（本板无外部编解码器，不使用）
 * ═══════════════════════════════════════════════════════════ */
#define FUNC_I2C_EN     (0)
#define GPIO_I2C_SCL    (GPIO_NUM_NC)
#define GPIO_I2C_SDA    (GPIO_NUM_NC)

/* ═══════════════════════════════════════════════════════════
 *  SDMMC / SDSPI（默认关闭）
 * ═══════════════════════════════════════════════════════════ */
#define FUNC_SDMMC_EN   (0)
#define SDMMC_BUS_WIDTH (1)
#define GPIO_SDMMC_CLK  (GPIO_NUM_NC)
#define GPIO_SDMMC_CMD  (GPIO_NUM_NC)
#define GPIO_SDMMC_D0   (GPIO_NUM_NC)
#define GPIO_SDMMC_D1   (GPIO_NUM_NC)
#define GPIO_SDMMC_D2   (GPIO_NUM_NC)
#define GPIO_SDMMC_D3   (GPIO_NUM_NC)
#define GPIO_SDMMC_DET  (GPIO_NUM_NC)

#define FUNC_SDSPI_EN       (0)
#define SDSPI_HOST          (SPI2_HOST)
#define GPIO_SDSPI_CS       (GPIO_NUM_NC)
#define GPIO_SDSPI_SCLK     (GPIO_NUM_NC)
#define GPIO_SDSPI_MISO     (GPIO_NUM_NC)
#define GPIO_SDSPI_MOSI     (GPIO_NUM_NC)

/* ═══════════════════════════════════════════════════════════
 *  I2S GPIO — INMP441 麦克风引脚定义
 *  根据实际接线修改以下值
 * ═══════════════════════════════════════════════════════════ */
#define FUNC_I2S_EN         (1)
#define GPIO_I2S_LRCK       (GPIO_NUM_5)   /* WS (字选择/帧同步) */
#define GPIO_I2S_MCLK       (GPIO_NUM_NC)  /* INMP441 不需要 MCLK */
#define GPIO_I2S_SCLK       (GPIO_NUM_4)   /* SCK (位时钟) */
#define GPIO_I2S_SDIN       (GPIO_NUM_6)   /* SD (串行数据输入) */
#define GPIO_I2S_DOUT       (GPIO_NUM_NC)  /* 无播放输出 */

/* ═══════════════════════════════════════════════════════════
 *  第二路 I2S（不使用）
 * ═══════════════════════════════════════════════════════════ */
#define FUNC_I2S0_EN         (0)
#define GPIO_I2S0_LRCK       (GPIO_NUM_NC)
#define GPIO_I2S0_MCLK       (GPIO_NUM_NC)
#define GPIO_I2S0_SCLK       (GPIO_NUM_NC)
#define GPIO_I2S0_SDIN       (GPIO_NUM_NC)
#define GPIO_I2S0_DOUT       (GPIO_NUM_NC)

/* ═══════════════════════════════════════════════════════════
 *  电源控制（不使用）
 * ═══════════════════════════════════════════════════════════ */
#define FUNC_PWR_CTRL       (0)
#define GPIO_PWR_CTRL       (GPIO_NUM_NC)
#define GPIO_PWR_ON_LEVEL   (1)

/* ═══════════════════════════════════════════════════════════
 *  INMP441 音频参数
 * ═══════════════════════════════════════════════════════════ */

/**
 * @brief I2S 声道选择
 *
 * INMP441 的 L/R 引脚决定数据出现在哪个声道：
 *   L/R → GND : 数据在左声道 → 使用 I2S_STD_SLOT_LEFT
 *   L/R → VDD : 数据在右声道 → 使用 I2S_STD_SLOT_RIGHT
 */
#define INMP441_I2S_SLOT     I2S_STD_SLOT_LEFT

/**
 * @brief 32-bit 转 16-bit 的位移量
 *
 * INMP441 输出 24-bit 数据，左对齐在 32-bit 帧中（有效位在 bit[31:8]）。
 * 右移后取低 16 位作为最终采样值：
 *
 *   位移量 = 16 : 取 bit[31:16]，无增益（推荐初始调试值）
 *   位移量 = 14 : 取 bit[29:14]，约 +12dB 增益（安静环境可用）
 *   位移量 = 12 : 取 bit[27:12]，约 +24dB 增益（极安静环境，易削波）
 *
 * 建议：先用 16 确认功能正常，再根据实际环境逐步减小位移量提升灵敏度。
 */
#define INMP441_BIT_SHIFT    (14)

/* ═══════════════════════════════════════════════════════════
 *  I2S 配置宏（兼容 IDF 5.x 新驱动和旧版驱动）
 * ═══════════════════════════════════════════════════════════ */

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

/**
 * IDF 5.x 新 I2S 驱动配置
 * 参数: sample_rate — 采样率 (Hz)
 *       channel_fmt — I2S_SLOT_MODE_MONO / I2S_SLOT_MODE_STEREO
 *       bits_per_chan — 每采样位数 (32 for INMP441)
 */
#define I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan) { \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate), \
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits_per_chan, channel_fmt), \
        .gpio_cfg = { \
            .mclk = GPIO_I2S_MCLK, \
            .bclk = GPIO_I2S_SCLK, \
            .ws   = GPIO_I2S_LRCK, \
            .dout = GPIO_I2S_DOUT, \
            .din  = GPIO_I2S_SDIN, \
            .invert_flags = { \
                .mclk_inv = false, \
                .bclk_inv = false, \
                .ws_inv   = false, \
            }, \
        }, \
    }

#else

/**
 * IDF 4.x 旧 I2S 驱动配置（向后兼容）
 */
#define I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan) { \
    .mode                   = I2S_MODE_MASTER | I2S_MODE_RX, \
    .sample_rate            = sample_rate, \
    .bits_per_sample        = I2S_BITS_PER_SAMPLE_32BIT, \
    .channel_format         = I2S_CHANNEL_FMT_ONLY_LEFT, \
    .communication_format   = I2S_COMM_FORMAT_STAND_I2S, \
    .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1, \
    .dma_buf_count          = 6, \
    .dma_buf_len            = 160, \
    .use_apll               = false, \
    .tx_desc_auto_clear     = true, \
    .fixed_mclk             = 0, \
    .mclk_multiple          = I2S_MCLK_MULTIPLE_DEFAULT, \
    .bits_per_chan           = I2S_BITS_PER_CHAN_32BIT, \
}

#endif
