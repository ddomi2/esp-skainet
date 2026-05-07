# ESP32-S3 + INMP441 语音唤醒词检测

## 概述

本项目基于 ESP-Skainet 的 `wake_word_detection/afe` 示例，新增了 **INMP441 数字麦克风** 硬件支持，使用 ESP32-S3-DevKitC-1 开发板直连 INMP441 实现语音关键字唤醒功能。

---

## 硬件连接

| INMP441 引脚 | ESP32-S3 GPIO | 说明 |
|:---:|:---:|:---|
| SCK | GPIO 4 | I2S 时钟 |
| WS | GPIO 5 | I2S 字选择（左/右声道） |
| SD | GPIO 6 | I2S 数据输入 |
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

### 3. 新增板级头文件

**文件：** `components/hardware_driver/boards/include/esp32_s3_devkitc_1_board.h`

定义内容：
- I2S GPIO 引脚映射（SCK=4, WS=5, SD=6）
- `INMP441_I2S_SLOT` — 声道选择（左声道 = L/R 接 GND）
- `INMP441_BIT_SHIFT` — 32 位转 16 位的位移量（14 = +12dB 增益，16 = 无增益）
- `I2S_CONFIG_DEFAULT` 宏 — 兼容 IDF 5.x 和旧版本的 I2S 配置

### 4. 新增板级驱动实现

**文件：** `components/hardware_driver/boards/esp32s3-devkitc-1/bsp_board.c`

核心逻辑：

```
┌─────────────────────────────────────────────────────────┐
│  INMP441 (I2S, 32-bit frames)                           │
│       │                                                  │
│       ▼                                                  │
│  i2s_channel_read() — 读取 32-bit 原始数据               │
│       │                                                  │
│       ▼                                                  │
│  bsp_get_feed_data() — 转换为 16-bit 双通道数据          │
│       │   raw[i] >> INMP441_BIT_SHIFT → sample           │
│       │   out[i*2]   = sample  (麦克风通道)              │
│       │   out[i*2+1] = 0       (空参考通道)              │
│       │                                                  │
│       ▼                                                  │
│  AFE 引擎 (输入格式 "MN" = 1 Mic + Null reference)      │
│       │                                                  │
│       ▼                                                  │
│  唤醒词检测结果                                          │
└─────────────────────────────────────────────────────────┘
```

关键实现细节：
- **输入格式 "MN"**：1 路麦克风 + 1 路空参考，AFE 使用此格式进行回声消除
- **32→16 位转换**：INMP441 输出 24 位数据左对齐在 32 位帧中，通过右移取高 16 位
- **反向遍历**：转换循环从末尾开始，避免原地转换时覆盖未读数据
- **不支持播放**：此板级配置仅用于录音，无 DAC/功放输出

### 5. 头文件引用注册

**文件：** `components/hardware_driver/boards/include/bsp_board.h`

已在条件编译中新增：

```c
#elif CONFIG_ESP32_S3_DEVKITC_1_BOARD
    #include "esp32_s3_devkitc_1_board.h"
```

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
I (xxx) devkitc1_board: Initializing ESP32-S3-DevKitC-1 with INMP441
I (xxx) devkitc1_board: I2S pins — SCK: 4, WS: 5, SD: 6
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

然后重新进入 menuconfig。

### Q: 编译报错找不到 `esp32_s3_devkitc_1_board.h`？

**A:** 确认以下文件存在：
- `components/hardware_driver/boards/include/esp32_s3_devkitc_1_board.h`
- `components/hardware_driver/boards/esp32s3-devkitc-1/bsp_board.c`

且 `CMakeLists.txt` 中有对应的 `if(CONFIG_ESP32_S3_DEVKITC_1_BOARD)` 块。

### Q: 烧录后没有唤醒响应？

排查步骤：
1. 确认 INMP441 接线正确（特别是 L/R 引脚接 GND）
2. 检查串口是否输出 `I2S init failed` 错误
3. 尝试将 `INMP441_BIT_SHIFT` 从 14 改为 16（降低增益，避免削波）
4. 靠近麦克风清晰说出唤醒词

### Q: 如何更改 GPIO 引脚？

修改 `esp32_s3_devkitc_1_board.h` 中的定义：

```c
#define GPIO_I2S_SCLK   (GPIO_NUM_4)   // 改为你的 SCK 引脚
#define GPIO_I2S_LRCK   (GPIO_NUM_5)   // 改为你的 WS 引脚
#define GPIO_I2S_SDIN   (GPIO_NUM_6)   // 改为你的 SD 引脚
```

---

## 文件清单

```
components/hardware_driver/
├── Kconfig.projbuild                          # 新增 INMP441 板级选项
├── CMakeLists.txt                             # 新增编译条件
├── boards/
│   ├── esp32s3-devkitc-1/
│   │   └── bsp_board.c                       # ★ 新增：INMP441 驱动实现
│   └── include/
│       ├── bsp_board.h                        # 已修改：新增 #elif 引用
│       └── esp32_s3_devkitc_1_board.h         # ★ 新增：引脚与配置定义
```
