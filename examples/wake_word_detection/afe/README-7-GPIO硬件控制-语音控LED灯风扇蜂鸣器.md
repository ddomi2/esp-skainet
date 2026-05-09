# GPIO 硬件控制 — 语音控制 LED + 风扇 + 蜂鸣器

> 本文档说明如何通过语音命令实际控制 ESP32-S3 的 GPIO 引脚。
> 已实现：LED 灯（GPIO 2）、风扇（GPIO 7）、蜂鸣器 PWM 音量控制（GPIO 9）。

---

## 目录

1. [硬件接线](#1-硬件接线)
2. [代码结构（重构后）](#2-代码结构重构后)
3. [工作原理](#3-工作原理)
4. [蜂鸣器 PWM 音量控制](#4-蜂鸣器-pwm-音量控制)
5. [如何扩展新设备](#5-如何扩展新设备)
6. [引脚使用情况](#6-引脚使用情况)
7. [编译烧录](#7-编译烧录)

---

## 1. 硬件接线

### 完整接线图

```
ESP32-S3-DevKitC-1
┌────────────────┐
│            GPIO2├───── 330Ω ───── LED (+) ───── GND
│            GPIO7├───── 风扇信号 / 继电器 IN ────┐
│            GPIO9├───── 蜂鸣器 (+) ─────── GND   │
│                │                               │
│            GPIO4├───── INMP441 SCK              │
│            GPIO5├───── INMP441 WS          [风扇/继电器]
│            GPIO6├───── INMP441 SD              │
│                │                               │
│            3.3V├───── INMP441 VDD              │
│             GND├───── INMP441 GND / 共地 ──────┘
└────────────────┘
```

### 材料清单

| 材料 | 数量 | 说明 |
|------|:---:|------|
| LED（任意颜色） | 1 | 建议用红色或绿色，容易观察 |
| 330Ω 电阻 | 1 | 限流保护，防止烧坏 LED |
| 小风扇 或 继电器模块 | 1 | 3.3V 小风扇可直驱；220V 设备用继电器 |
| 有源蜂鸣器 或 无源蜂鸣器 | 1 | 无源蜂鸣器效果更好（可调音） |
| 杜邦线 | 若干 | 连接各引脚 |

### LED 接线

```
GPIO 2 → 330Ω 电阻 → LED 正极 (+) → GND
```

### 风扇接线方式

**方式 A — 直驱 3.3V 小风扇（实验用）：**
```
GPIO 7 → 风扇正极 (+)
GND    → 风扇负极 (-)
```
> ⚠️ GPIO 最大输出电流约 40mA，只能驱动微型风扇

**方式 B — 继电器模块控制大功率风扇（推荐）：**
```
GPIO 7 → 继电器模块 IN
3.3V   → 继电器模块 VCC
GND    → 继电器模块 GND
风扇   → 继电器 COM/NO 端（接市电需注意安全！）
```

### 蜂鸣器接线

```
GPIO 9 → 蜂鸣器正极 (+)
GND    → 蜂鸣器负极 (-)
```

> 💡 **有源 vs 无源蜂鸣器：**
> - **有源蜂鸣器**：内部自带振荡电路，给电就响，PWM 可调音量
> - **无源蜂鸣器**：需要 PWM 方波驱动，频率决定音调，占空比决定音量（推荐）

---

## 2. 代码结构（模块化重构）

### 设计思路

> **一个设备 = 一对 .h/.c 文件**

之前 LED、风扇、蜂鸣器全部混在一个 `gpio_ctrl.c` 里（200+ 行），对不熟悉项目的人来说很难分清哪段代码控制哪个设备。

重构原则：
1. **职责单一** — 每个文件只做一件事，改风扇不用看 LED 代码
2. **伞形头文件** — 外部只需 `#include "gpio_ctrl.h"` 就能用全部设备
3. **统一入口** — `gpio_ctrl_init()` 内部依次调各子模块 init，调用者无感知
4. **易扩展** — 新设备只需创建 `gpio_xxx.h/c`，在 `gpio_ctrl.h` 加一行 `#include`

### 文件结构

```
main/
├── CMakeLists.txt      ← 注册所有 .c 文件（新增设备在此追加）
├── main.c              ← 入口：任务 + app_main（不碰设备细节）
├── cmd_handler.h/c     ← 命令词注册 + 动作执行分发
├── gpio_ctrl.h         ← 🎯 总控头文件（#include 所有设备 + 统一 init）
├── gpio_ctrl.c         ← 🎯 统一初始化入口（~30 行，调用各子模块 init）
├── gpio_led.h/c        ← 💡 LED 灯（GPIO 2，简单高低电平）
├── gpio_fan.h/c        ← 🌀 风扇（GPIO 7，简单高低电平）
└── gpio_buzzer.h/c     ← 🔔 蜂鸣器（GPIO 9，LEDC PWM 音量控制）
```

### 调用关系图

```
┌─────────────────────────────────────────────────────────┐
│  main.c                                                 │
│    #include "gpio_ctrl.h"  ← 只引这一个头文件           │
│    gpio_ctrl_init();       ← 只调这一个 init            │
└──────────────┬──────────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────────┐
│  gpio_ctrl.h  (伞形头文件)                               │
│    #include "gpio_led.h"                                │
│    #include "gpio_fan.h"                                │
│    #include "gpio_buzzer.h"                             │
│    esp_err_t gpio_ctrl_init(void);                      │
└──────────────┬──────────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────────┐
│  gpio_ctrl.c  (统一初始化入口)                           │
│    gpio_led_init();     → 配置 GPIO 2 为输出            │
│    gpio_fan_init();     → 配置 GPIO 7 为输出            │
│    gpio_buzzer_init();  → 配置 LEDC PWM (GPIO 9)       │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  cmd_handler.c                                          │
│    #include "gpio_ctrl.h"  ← 同样只引这一个头文件       │
│    case ACT_LIGHT_ON:  gpio_led_set(true);              │
│    case ACT_FAN_ON:    gpio_fan_set(true);              │
│    case ACT_MUSIC_PLAY: gpio_buzzer_set(true);          │
│    case ACT_VOL_UP:    gpio_buzzer_vol_up();            │
└─────────────────────────────────────────────────────────┘
```

### 各文件职责

| 文件 | 行数 | 职责 | 改动场景 |
|------|:---:|------|---------|
| `main.c` | ~242 | 初始化 + feed_Task + detect_Task | 改识别流程 |
| `cmd_handler.c` | ~670 | 命令词数据 + 注册 + 动作分发 | 添加新命令词/动作 |
| `gpio_ctrl.h` | ~35 | 伞形头文件（include 子模块） | 新增设备时加一行 include |
| `gpio_ctrl.c` | ~30 | 统一 init 入口 | 新增设备时加一行 init 调用 |
| `gpio_led.c` | ~50 | LED 控制（set/get/toggle） | 只改 LED 逻辑 |
| `gpio_fan.c` | ~45 | 风扇控制（set/get） | 只改风扇逻辑 |
| `gpio_buzzer.c` | ~100 | 蜂鸣器 PWM（set/vol_up/vol_down） | 只改蜂鸣器逻辑 |

### 好处

- **修改命令词** → 只改 `cmd_handler.c`
- **添加新设备** → 创建 `gpio_xxx.h/c`，`gpio_ctrl.h` 加一行 include
- **修改某个设备** → 只碰那一个文件（如改蜂鸣器频率只改 `gpio_buzzer.c`）
- **修改识别流程** → 只改 `main.c`
- 互不影响，增量编译也更快（只重编改动的 .c）

---

## 3. 工作原理

```
用户说 "打开灯" / "打开风扇" / "播放音乐"
       │
       ▼
┌────────────────────┐
│  INMP441 麦克风     │  采集 16kHz PCM 音频
└────────┬───────────┘
         │ I2S
         ▼
┌────────────────────┐
│  AFE 引擎          │  VAD 语音活动检测 + 降噪
└────────┬───────────┘
         │
         ▼
┌────────────────────┐
│  WakeNet 唤醒词     │  检测 "嗨，乐鑫"
└────────┬───────────┘
         │ 唤醒成功
         ▼
┌────────────────────┐
│  MultiNet 命令词    │  识别 → string 匹配
└────────┬───────────┘
         │
         ▼
┌────────────────────┐
│  cmd_handler_execute│  strcmp → action_map[]
└────────┬───────────┘
         │
    ┌────┼────────┐
    ▼    ▼        ▼
┌──────┐┌──────┐┌──────────┐
│GPIO 2││GPIO 7││GPIO 9    │
│LED   ││风扇  ││蜂鸣器PWM │
└──────┘└──────┘└──────────┘
```

### 所有语音命令 → 硬件对应表

| 语音命令 | 拼音 | 动作 | 硬件效果 |
|---------|------|------|---------|
| "打开灯" / "开灯" / "打开电灯" | da kai deng / kai deng / da kai dian deng | ACT_LIGHT_ON | GPIO 2 → HIGH (LED 亮) |
| "关灯" / "关闭灯" / "关闭电灯" | guan deng / guan bi deng / guan bi dian deng | ACT_LIGHT_OFF | GPIO 2 → LOW (LED 灭) |
| "打开风扇" / "打开风机" | da kai feng shan / da kai feng ji | ACT_FAN_ON | GPIO 7 → HIGH (风扇转) |
| "关闭风扇" / "关闭风机" | guan bi feng shan / guan bi feng ji | ACT_FAN_OFF | GPIO 7 → LOW (风扇停) |
| "播放音乐" | bo fang yin yue | ACT_MUSIC_PLAY | GPIO 9 PWM 开 (蜂鸣器响) |
| "暂停音乐" / "暂停" | zan ting yin yue / zan ting | ACT_MUSIC_PAUSE | GPIO 9 PWM 关 (蜂鸣器停) |
| "增大音量" / "大声一点" | zeng da yin liang / da sheng yi dian | ACT_VOL_UP | PWM 占空比 +10% |
| "减小音量" / "小声一点" | jian xiao yin liang / xiao sheng yi dian | ACT_VOL_DOWN | PWM 占空比 -10% |

---

## 4. 蜂鸣器 PWM 音量控制

### 原理

蜂鸣器使用 ESP-IDF 的 **LEDC（LED Control）** 外设产生 PWM 方波：

```
              占空比 = 50% (音量5)
         ┌──┐  ┌──┐  ┌──┐  ┌──┐
GPIO 9 ──┘  └──┘  └──┘  └──┘  └──  → 2000Hz 方波 → 蜂鸣器发声

              占空比 = 100% (音量10)
         ┌────────────────────────
GPIO 9 ──┘                          → 满功率 → 最大音量

              占空比 = 0% (静音)
         ────────────────────────── → 无输出 → 蜂鸣器不响
```

- **频率固定 2000Hz**：产生清晰的"嘟"声
- **占空比 0~100%**：对应音量等级 0~10
- **10 位分辨率**：占空比精度 0~1023

### 音量等级对应表

| 音量等级 | 占空比 | 实际 duty 值 | 听感 |
|:---:|:---:|:---:|------|
| 1 | 10% | 102 | 很轻 |
| 2 | 20% | 204 | 较轻 |
| 3 | 30% | 307 | 轻 |
| 4 | 40% | 409 | 偏小 |
| **5** | **50%** | **511** | **默认（中等）** |
| 6 | 60% | 614 | 偏大 |
| 7 | 70% | 716 | 较大 |
| 8 | 80% | 819 | 大 |
| 9 | 90% | 921 | 很大 |
| 10 | 100% | 1023 | 最大 |

### 关键代码

**gpio_buzzer.c** — LEDC 初始化：

```c
/* LEDC 定时器 → 控制频率 */
ledc_timer_config_t timer_conf = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .timer_num       = LEDC_TIMER_0,
    .duty_resolution = LEDC_TIMER_10_BIT,  /* 0~1023 */
    .freq_hz         = 2000,               /* 2kHz 蜂鸣器音调 */
    .clk_cfg         = LEDC_AUTO_CLK,
};

/* LEDC 通道 → 绑定 GPIO + 控制占空比 */
ledc_channel_config_t ch_conf = {
    .gpio_num   = GPIO_BUZZER_PIN,   /* GPIO 9 */
    .channel    = LEDC_CHANNEL_0,
    .timer_sel  = LEDC_TIMER_0,
    .duty       = 0,                 /* 初始关闭 */
};
```

**gpio_buzzer.c** — 音量调节核心逻辑：

```c
static void buzzer_update_duty(void)
{
    if (!s_state) {
        ledc_set_duty(mode, channel, 0);       /* 关闭 → duty=0 */
    } else {
        /* 音量 1~10 → 占空比 10%~100% */
        uint32_t duty = (1023 * s_vol) / 10;
        ledc_set_duty(mode, channel, duty);
    }
    ledc_update_duty(mode, channel);           /* 立即生效 */
}
```

---

## 5. 如何扩展新设备

### 四步添加法（以"窗帘舵机"为例）

**第一步：创建 `gpio_curtain.h` — 定义引脚 + 声明接口**

```c
/* gpio_curtain.h */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define GPIO_CURTAIN_PIN  11   /* 舵机信号引脚 */

esp_err_t gpio_curtain_init(void);
void gpio_curtain_set(bool open);
bool gpio_curtain_get(void);
```

**第二步：创建 `gpio_curtain.c` — 实现控制逻辑**

```c
/* gpio_curtain.c */
#include "gpio_curtain.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_curtain";
static bool s_state = false;

esp_err_t gpio_curtain_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_CURTAIN_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(GPIO_CURTAIN_PIN, 0);
    ESP_LOGI(TAG, "窗帘初始化完成 (GPIO%d)", GPIO_CURTAIN_PIN);
    return ESP_OK;
}

void gpio_curtain_set(bool open)
{
    s_state = open;
    gpio_set_level(GPIO_CURTAIN_PIN, open ? 1 : 0);
    ESP_LOGI(TAG, "窗帘 %s", open ? "打开 🪟" : "关闭 ⬛");
}
```

**第三步：注册到系统**

```c
/* gpio_ctrl.h — 加一行 include */
#include "gpio_curtain.h"

/* gpio_ctrl.c — init 中加一行 */
ret = gpio_curtain_init();
if (ret != ESP_OK) return ret;

/* CMakeLists.txt — SRCS 追加 */
idf_component_register(SRCS ... gpio_curtain.c ...)

/* cmd_handler.c — switch 中添加 case */
case ACT_CURTAIN_OPEN:
    gpio_curtain_set(true);
    break;
case ACT_CURTAIN_CLOSE:
    gpio_curtain_set(false);
    break;
```

**第四步：编译验证**

```bash
idf.py build && idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 扩展流程图

```
新设备需求（如：窗帘）
        │
        ▼
┌──────────────────┐
│ 1. 新建 gpio_xxx.h │  定义引脚 + 接口声明
│ 2. 新建 gpio_xxx.c │  实现 init + set/get
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 3. gpio_ctrl.h    │  加 #include "gpio_xxx.h"
│ 4. gpio_ctrl.c    │  加 gpio_xxx_init() 调用
│ 5. CMakeLists.txt │  加 gpio_xxx.c 到 SRCS
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 6. cmd_handler.c  │  switch 中加 case 调用
└────────┬─────────┘
         │
         ▼
    idf.py build ✅
```

---

## 6. 引脚使用情况

### ESP32-S3-DevKitC-1 (N16R8) 引脚分配

| GPIO | 用途 | 驱动方式 | 状态 |
|:---:|------|------|:---:|
| 0 | Boot 按键 | — | ⚠️ 保留 |
| 1 | 可用 | — | 🟢 空闲 |
| **2** | **LED 灯** | GPIO 高低电平 | 🔴 已占用 |
| 3 | 可用 | — | 🟢 空闲 |
| **4** | **I2S SCK (INMP441)** | I2S 外设 | 🔴 已占用 |
| **5** | **I2S WS (INMP441)** | I2S 外设 | 🔴 已占用 |
| **6** | **I2S SD (INMP441)** | I2S 外设 | 🔴 已占用 |
| **7** | **风扇控制** | GPIO 高低电平 | 🔴 已占用 |
| 8 | 可用 | — | 🟢 空闲 |
| **9** | **蜂鸣器** | LEDC PWM (2kHz) | 🔴 已占用 |
| 10~21 | 可用 | — | 🟢 空闲 |
| 26~37 | Flash/PSRAM (N16R8) | — | ❌ 不可用 |
| 38~48 | 可用 | — | 🟢 空闲 |

### 推荐扩展引脚

| 设备 | 推荐引脚 | 驱动方式 |
|------|:---:|------|
| 继电器（空调） | GPIO 8 | GPIO 高低电平 |
| 红外发射器 | GPIO 10 | RMT 外设 |
| 舵机 (窗帘) | GPIO 11 | LEDC PWM (50Hz) |
| RGB LED (WS2812) | GPIO 48 | RMT/SPI |
| 温湿度传感器 | GPIO 12 | 单总线 |

---

## 7. 编译烧录

```bash
# 编译
idf.py build

# 烧录并监控串口输出
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 预期输出

```
I (xxx) gpio_ctrl: GPIO 初始化完成 — LED: GPIO2, 风扇: GPIO7, 蜂鸣器: GPIO9 (PWM 2000Hz)
...
┌─── 注册内置命令词: 313 成功, 0 失败 ───┐
│ 注册自定义命令词: 19 成功, 0 失败
└─── 总计: 332 条命令词 ───┘
============ detect start ============
说出唤醒词 "嗨，乐鑫" 以激活命令词识别...
```

### 测试步骤

| 步骤 | 操作 | 预期结果 |
|:---:|------|---------|
| 1 | 说 "嗨，乐鑫" → "打开灯" | LED 亮 💡 |
| 2 | 说 "嗨，乐鑫" → "关灯" | LED 灭 ⚫ |
| 3 | 说 "嗨，乐鑫" → "打开风扇" | 风扇转 🌀 |
| 4 | 说 "嗨，乐鑫" → "关闭风扇" | 风扇停 ⚫ |
| 5 | 说 "嗨，乐鑫" → "播放音乐" | 蜂鸣器响 🔔 (音量5) |
| 6 | 说 "嗨，乐鑫" → "大声一点" | 蜂鸣器变大 🔊 (音量6) |
| 7 | 说 "嗨，乐鑫" → "小声一点" | 蜂鸣器变小 🔉 (音量4) |
| 8 | 说 "嗨，乐鑫" → "暂停" | 蜂鸣器停 🔇 |

---

## 总结

| 版本 | 控制能力 | 驱动方式 |
|------|---------|---------|
| v1 (README-7 初版) | 只有 LED | GPIO 高低电平 |
| v2 (添加风扇) | LED + 风扇 | GPIO 高低电平 |
| **v3 (当前)** | **LED + 风扇 + 蜂鸣器** | **GPIO + LEDC PWM** |

### 架构总览

```
语音输入 → AFE → WakeNet → MultiNet → cmd_handler → gpio_ctrl
                                                        │
                                        ┌───────────────┼───────────────┐
                                        ▼               ▼               ▼
                                   GPIO 2 (LED)    GPIO 7 (风扇)    GPIO 9 (蜂鸣器)
                                   开/关            开/关            开/关/音量±
```

---

> 📖 相关文档：
> - `README-5-自定义命令词.md` — 命令词注册与踩坑记录
> - `README-1-驱动优化-INMP441.md` — INMP441 硬件驱动
> - `README-6-TODO-能力路线图.md` — 项目能力全景与 TODO
