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

## 2. 代码结构（重构后）

原来所有代码都在 `main.c`（780+ 行），现在拆分为 3 个模块：

```
main/
├── CMakeLists.txt     ← 构建注册（3个 .c 文件）
├── main.c             ← 入口：任务函数 + app_main（~242 行）
├── cmd_handler.h      ← 命令词处理接口
├── cmd_handler.c      ← 313 内置 + 自定义命令 + 动作映射 + 执行
├── gpio_ctrl.h        ← GPIO 硬件控制接口（LED / 风扇 / 蜂鸣器）
└── gpio_ctrl.c        ← 硬件实现：GPIO + LEDC PWM
```

### 各文件职责

| 文件 | 职责 |
|------|------|
| `main.c` | 初始化 + feed_Task + detect_Task |
| `cmd_handler.c` | 命令词数据 + 注册逻辑 + 动作执行分发 |
| `gpio_ctrl.c` | GPIO 初始化 + LED / 风扇 / 蜂鸣器 PWM 控制 |

### 好处

- **修改命令词** → 只改 `cmd_handler.c`
- **添加新设备** → 只改 `gpio_ctrl.c` + `gpio_ctrl.h`
- **修改识别流程** → 只改 `main.c`
- 互不影响，编译也更快（增量编译只重编改动的文件）

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

**gpio_ctrl.c** — LEDC 初始化：

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

**gpio_ctrl.c** — 音量调节核心逻辑：

```c
static void buzzer_update_duty(void)
{
    if (!s_buzzer_state) {
        ledc_set_duty(mode, channel, 0);       /* 关闭 → duty=0 */
    } else {
        /* 音量 1~10 → 占空比 10%~100% */
        uint32_t duty = (1023 * s_buzzer_vol) / 10;
        ledc_set_duty(mode, channel, duty);
    }
    ledc_update_duty(mode, channel);           /* 立即生效 */
}
```

---

## 5. 如何扩展新设备

### 三步添加法

**第一步：`gpio_ctrl.h` — 定义引脚 + 声明接口**

```c
#define GPIO_NEW_DEVICE_PIN  10  /* 选个空闲引脚 */
void gpio_ctrl_new_device_set(bool on);
```

**第二步：`gpio_ctrl.c` — 实现控制逻辑**

```c
void gpio_ctrl_new_device_set(bool on)
{
    gpio_set_level(GPIO_NEW_DEVICE_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "新设备 %s", on ? "ON" : "OFF");
}
```

别忘了在 `gpio_ctrl_init()` 的 `pin_bit_mask` 中加上新引脚。

**第三步：`cmd_handler.c` — 在 switch 中添加 case**

```c
case ACT_NEW_DEVICE_ON:
    gpio_ctrl_new_device_set(true);
    break;
```

**编译验证：**
```bash
idf.py build && idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
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
