# ESP32-S3 + INMP441 语音唤醒词检测

## 概述

本项目基于 ESP-Skainet 的 `wake_word_detection/afe` 示例，新增了 **INMP441 数字麦克风** 硬件支持，使用 ESP32-S3-DevKitC-1 开发板直连 INMP441 实现语音关键字唤醒功能。

---

## 硬件连接

| INMP441 引脚 | ESP32-S3 GPIO | 说明 |
|:---:|:---:|:---|
| SCK | GPIO 4 | I2S 位时钟 (BCLK) |
| WS | GPIO 5 | I2S 字选择 / 帧同步 (LRCLK) |
| SD | GPIO 6 | I2S 串行数据输入 |
| L/R | GND | 选择左声道（接 VDD 则为右声道） |
| VDD | 3.3V | 电源 |
| GND | GND | 地 |

> ⚠️ ESP32-S3 N16R8（Octal SPI）的 GPIO 26-37 被 Flash/PSRAM 占用，不可使用。

---

## 代码修改说明

### 1. 新增 Kconfig 选项

**文件：** `components/hardware_driver/Kconfig.projbuild`

在 `choice AUDIO_BOARD` 中新增：

```kconfig
config ESP32_S3_DEVKITC_1_BOARD
    bool "ESP32-S3-DevKitC-1 (INMP441)"
    depends on IDF_TARGET_ESP32S3
```

> 注意：`depends on IDF_TARGET_ESP32S3` 表示此选项仅在目标芯片设置为 ESP32-S3 时可见。如果 menuconfig 中看不到此选项，说明当前 IDF_TARGET 不是 esp32s3。

### 2. 新增 CMake 编译条件

**文件：** `components/hardware_driver/CMakeLists.txt`

新增：

```cmake
if(CONFIG_ESP32_S3_DEVKITC_1_BOARD)
    list(APPEND BSP_BOARD_SRC "./boards/esp32s3-devkitc-1")
endif()
```

### 3. 新增板级头文件（含优化）

**文件：** `components/hardware_driver/boards/include/esp32_s3_devkitc_1_board.h`

定义内容：
- I2S GPIO 引脚映射（SCK=4, WS=5, SD=6），附中文注释说明每个引脚功能
- `INMP441_I2S_SLOT` — 声道选择（左声道 = L/R 接 GND）
- `INMP441_BIT_SHIFT` — 32 位转 16 位的位移量（**已优化：默认改为 16，无增益，更安全**）
- `I2S_CONFIG_DEFAULT` 宏 — 兼容 IDF 5.x 和旧版本的 I2S 配置

### 4. 新增板级驱动实现（含优化）

**文件：** `components/hardware_driver/boards/esp32s3-devkitc-1/bsp_board.c`

核心数据流：

```
┌─────────────────────────────────────────────────────────┐
│  INMP441 (I2S, 32-bit frames, 24-bit 有效数据)          │
│       │                                                  │
│       ▼                                                  │
│  i2s_channel_read() — 读取 32-bit 原始采样               │
│       │                                                  │
│       ▼                                                  │
│  bsp_get_feed_data() — 原地转换为 16-bit 双通道          │
│       │   raw[i] >> INMP441_BIT_SHIFT → 16-bit sample    │
│       │   out[i*2]   = sample  (麦克风通道)              │
│       │   out[i*2+1] = 0       (空参考通道)              │
│       │                                                  │
│       ▼                                                  │
│  AFE 引擎 (输入格式 "MN" = 1 Mic + Null reference)      │
│       │                                                  │
│       ▼                                                  │
│  WakeNet 唤醒词检测                                      │
└─────────────────────────────────────────────────────────┘
```

### 5. 头文件引用注册

**文件：** `components/hardware_driver/boards/include/bsp_board.h`

已在条件编译中新增：

```c
#elif CONFIG_ESP32_S3_DEVKITC_1_BOARD
    #include "esp32_s3_devkitc_1_board.h"
```

---

## 本次优化内容

### 优化 1：`bsp_board_init` — 使用传入的采样率参数

**问题：** 原代码硬编码 `16000`，忽略调用者传入的 `sample_rate` 参数。

**修复前：**
```c
esp_err_t ret = bsp_i2s_init(I2S_NUM_1, 16000, 2, 32);
```

**修复后：**
```c
esp_err_t ret = bsp_i2s_init(I2S_NUM_1, sample_rate);
```

> 虽然唤醒词检测固定用 16kHz，但保持接口正确性，未来扩展其他场景不会出错。

---

### 优化 2：`bsp_i2s_init` — 精简函数签名

**问题：** 原函数有 `channel_format` 和 `bits_per_chan` 参数，但 INMP441 配置固定（单声道 32-bit），这些参数从未被外部动态指定。

**修复前：**
```c
static esp_err_t bsp_i2s_init(i2s_port_t i2s_num, uint32_t sample_rate,
                              int channel_format, int bits_per_chan)
```

**修复后：**
```c
static esp_err_t bsp_i2s_init(i2s_port_t i2s_num, uint32_t sample_rate)
```

> INMP441 的通道格式和位深是硬件决定的，不应作为可变参数暴露。

---

### 优化 3：`bsp_get_feed_data` — 处理 I2S 读取不足

**问题：** 如果 `i2s_channel_read` 实际读取的字节数少于请求值（例如 DMA 超时），转换循环会处理未初始化的内存数据，导致噪声输入 AFE。

**修复前：**
```c
int audio_chunksize = buffer_len / (sizeof(int32_t));
// ... 直接用 audio_chunksize 遍历全部
```

**修复后：**
```c
/* 根据实际读到的字节数计算有效采样数 */
int actual_samples = bytes_read / sizeof(int32_t);
// ... 用 actual_samples 遍历

/* 不足部分填零 */
int expected_samples = buffer_len / sizeof(int32_t);
if (actual_samples < expected_samples) {
    memset(&out[actual_samples * 2], 0,
           (expected_samples - actual_samples) * 2 * sizeof(int16_t));
}
```

---

### 优化 4：`INMP441_BIT_SHIFT` — 默认值改为 16（无增益）

**问题：** 原始值 14 相当于 +12dB 增益，在较响的环境下容易削波（clipping），导致唤醒词识别率下降。

**修复前：**
```c
#define INMP441_BIT_SHIFT    (14)
```

**修复后：**
```c
#define INMP441_BIT_SHIFT    (16)
```

> 位移 16 = 取 bit[31:16]，无增益放大。确认功能正常后，如需提高灵敏度再改为 14 或 12。

---

### 优化 5：移除未使用的全局变量

**问题：** `s_play_sample_rate`、`s_play_channel_format`、`s_bits_per_chan` 从未被引用（本板不支持播放）。

**修复：** 删除这三个变量，减少内存占用和编译警告。

---

### 优化 6：关键位置添加中文注释

所有关键逻辑点（I2S 配置原因、数据转换算法、AFE 格式选择等）已添加中文注释，方便后续维护。

---

## 构建与烧录步骤

### 前提条件

- 已安装 ESP-IDF v5.5.x（本项目使用 5.5.3）
- 已配置 `IDF_PATH` 环境变量
- ESP32-S3-DevKitC-1 通过 USB 连接

### 步骤 1：设置目标芯片为 ESP32-S3

```bash
cd examples/wake_word_detection/afe
idf.py set-target esp32s3
```

> 此命令会清除 `build/` 目录并重新生成 sdkconfig（基于 `sdkconfig.defaults` + `sdkconfig.defaults.esp32s3`）。

### 步骤 2：通过 menuconfig 选择 INMP441 板级配置

```bash
idf.py menuconfig
```

导航路径：

```
(Top)
  → Audio Media HAL
    → Audio hardware board
      → [*] ESP32-S3-DevKitC-1 (INMP441)
```

按 `S` 保存，按 `Q` 退出。

### 步骤 3：编译项目

```bash
idf.py build
```

编译成功后会输出固件大小和分区信息。

### 步骤 4：烧录并监控串口输出

```bash
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

> 将 `/dev/cu.usbmodem5B8E0653461` 替换为你实际的串口设备路径。  
> 按 `Ctrl+]` 退出 monitor。

---

## 预期串口输出

正常运行时应看到类似输出：

```
I (xxx) devkitc1_board: 初始化 ESP32-S3-DevKitC-1 + INMP441
I (xxx) devkitc1_board: I2S 引脚 — SCK: 4, WS: 5, SD: 6
I (xxx) devkitc1_board: 采样率: 16000 Hz, 位移: 16
wakenet model in flash: wn9_hilexin
wakeword model in AFE config: wn9_hilexin
------------detect start------------
```

对着麦克风说 "Hi 乐鑫" 后：

```
wakeword detected
model index:1, word index:1
-----------LISTENING-----------
```

---

## 常见问题

### Q: menuconfig 中只看到 "ESP32-Korvo"，没有 INMP441 选项？

**A:** 当前 IDF_TARGET 不是 esp32s3。执行：

```bash
idf.py set-target esp32s3
```

然后重新进入 menuconfig。每个板级选项都有 `depends on IDF_TARGET_XXX` 约束，只有目标芯片匹配时才可见。

### Q: 编译报错找不到 `esp32_s3_devkitc_1_board.h`？

**A:** 确认以下文件存在：
- `components/hardware_driver/boards/include/esp32_s3_devkitc_1_board.h`
- `components/hardware_driver/boards/esp32s3-devkitc-1/bsp_board.c`

且 `CMakeLists.txt` 中有对应的 `if(CONFIG_ESP32_S3_DEVKITC_1_BOARD)` 块。

### Q: 烧录后没有唤醒响应？

排查步骤：
1. 确认 INMP441 接线正确（特别是 L/R 引脚接 GND）
2. 检查串口是否输出 `I2S 初始化失败` 错误
3. 如果环境安静，可将 `INMP441_BIT_SHIFT` 从 16 改为 14 提高灵敏度
4. 靠近麦克风（10cm 内）清晰说出唤醒词
5. 确认 `sdkconfig.defaults.esp32s3` 中启用了 `CONFIG_SR_WN_WN9_HILEXIN=y`

### Q: 如何更改 GPIO 引脚？

修改 `esp32_s3_devkitc_1_board.h` 中的定义：

```c
#define GPIO_I2S_SCLK   (GPIO_NUM_4)   /* 改为你的 SCK 引脚 */
#define GPIO_I2S_LRCK   (GPIO_NUM_5)   /* 改为你的 WS 引脚 */
#define GPIO_I2S_SDIN   (GPIO_NUM_6)   /* 改为你的 SD 引脚 */
```

### Q: 如何调整麦克风增益？

修改 `esp32_s3_devkitc_1_board.h` 中的 `INMP441_BIT_SHIFT`：

| 值 | 增益 | 适用场景 |
|:---:|:---:|:---|
| 16 | 0 dB | 默认值，正常环境 |
| 14 | +12 dB | 安静环境，距离稍远 |
| 12 | +24 dB | 极安静环境（易削波） |

---

## 文件清单

```
components/hardware_driver/
├── Kconfig.projbuild                          # 新增 INMP441 板级选项
├── CMakeLists.txt                             # 新增编译条件
├── boards/
│   ├── esp32s3-devkitc-1/
│   │   └── bsp_board.c                       # ★ INMP441 驱动（已优化）
│   └── include/
│       ├── bsp_board.h                        # 已修改：新增 #elif 引用
│       └── esp32_s3_devkitc_1_board.h         # ★ 引脚与配置（已优化）
```

---

## 优化变更汇总

| # | 文件 | 改动 | 原因 |
|:---:|:---|:---|:---|
| 1 | `bsp_board.c` | `bsp_board_init` 使用传入的 `sample_rate` | 接口正确性 |
| 2 | `bsp_board.c` | `bsp_i2s_init` 移除多余参数 | INMP441 配置固定 |
| 3 | `bsp_board.c` | `bsp_get_feed_data` 基于 `bytes_read` 处理 | 防止噪声数据 |
| 4 | `board.h` | `INMP441_BIT_SHIFT` 14→16 | 避免削波 |
| 5 | `bsp_board.c` | 删除未使用的全局变量 | 减少警告 |
| 6 | 两个文件 | 关键逻辑添加中文注释 | 可读性 |
