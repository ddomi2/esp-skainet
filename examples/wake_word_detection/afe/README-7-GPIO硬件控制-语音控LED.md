# GPIO 硬件控制 — 语音控制 LED + 风扇

> 本文档说明如何通过语音命令实际控制 ESP32-S3 的 GPIO 引脚。
> 已实现：LED 灯（GPIO 2）、风扇（GPIO 7）的语音开关控制。

---

## 目录

1. [硬件接线](#1-硬件接线)
2. [代码结构（重构后）](#2-代码结构重构后)
3. [工作原理](#3-工作原理)
4. [如何扩展新设备](#4-如何扩展新设备)
5. [引脚使用情况](#5-引脚使用情况)
6. [编译烧录](#6-编译烧录)

---

## 1. 硬件接线

### LED 接线图

```
ESP32-S3-DevKitC-1
┌────────────────┐
│            GPIO2├───── 330Ω ───── LED (+) ───── GND
│            GPIO7├───── 风扇信号 / 继电器 IN ────┐
│                │                               │
│            GPIO4├───── INMP441 SCK              │
│            GPIO5├───── INMP441 WS          [风扇/继电器]
│            GPIO6├───── INMP441 SD              │
│                │                               │
│            3.3V├───── INMP441 VDD              │
│             GND├───── INMP441 GND / LED (-) ───┘
└────────────────┘
```

### 材料清单

| 材料 | 数量 | 说明 |
|------|:---:|------|
| LED（任意颜色） | 1 | 建议用红色或绿色，容易观察 |
| 330Ω 电阻 | 1 | 限流保护，防止烧坏 LED |
| 小风扇 或 继电器模块 | 1 | 3.3V 小风扇可直驱；220V 设备用继电器 |
| 杜邦线 | 若干 | 连接各引脚 |

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

---

## 2. 代码结构（重构后）

原来所有代码都在 `main.c`（780+ 行），现在拆分为 3 个文件：

```
main/
├── CMakeLists.txt     ← 更新：注册新增的 .c 文件
├── main.c             ← 精简：只有任务函数 + app_main（~180 行）
├── cmd_handler.h      ← 命令词处理接口
├── cmd_handler.c      ← 313 内置 + 自定义命令 + 动作映射 + 执行
├── gpio_ctrl.h        ← GPIO 硬件控制接口
└── gpio_ctrl.c        ← LED 开关实现（后续扩展其他设备）
```

### 各文件职责

| 文件 | 行数 | 职责 |
|------|:---:|------|
| `main.c` | ~180 | 初始化 + feed_Task + detect_Task |
| `cmd_handler.c` | ~370 | 命令词数据 + 注册逻辑 + 执行逻辑 |
| `gpio_ctrl.c` | ~75 | GPIO 初始化 + LED 控制函数 |

### 好处

- **修改命令词** → 只改 `cmd_handler.c`
- **添加新设备** → 只改 `gpio_ctrl.c`
- **修改识别流程** → 只改 `main.c`
- 互不影响，编译也更快（增量编译只重编改动的文件）

---

## 3. 工作原理

```
用户说 "打开灯" / "打开风扇"
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
│  MultiNet 命令词    │  识别 → string=" da kai deng"
└────────┬───────────┘         或 " da kai feng shan"
         │
         ▼
┌────────────────────┐
│  cmd_handler_execute│  strcmp 匹配 action_map[]
│                    │  → ACT_LIGHT_ON / ACT_FAN_ON
└────────┬───────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌────────┐ ┌────────┐
│ GPIO 2 │ │ GPIO 7 │
│LED 亮  │ │风扇 转 │
└────────┘ └────────┘
```

### 支持的语音命令

| 语音命令 | 拼音 | 动作 | 硬件 |
|---------|------|------|------|
| "打开灯" / "开灯" / "打开电灯" | da kai deng / kai deng / da kai dian deng | ACT_LIGHT_ON | GPIO 2 → HIGH |
| "关灯" / "关闭灯" / "关闭电灯" | guan deng / guan bi deng / guan bi dian deng | ACT_LIGHT_OFF | GPIO 2 → LOW |
| "打开风扇" / "打开风机" | da kai feng shan / da kai feng ji | ACT_FAN_ON | GPIO 7 → HIGH |
| "关闭风扇" / "关闭风机" | guan bi feng shan / guan bi feng ji | ACT_FAN_OFF | GPIO 7 → LOW |

### 关键代码片段

**cmd_handler.c** — 执行硬件动作的部分：

```c
case ACT_LIGHT_ON:
    printf("💡 执行: 打开灯\n");
    gpio_ctrl_led_set(true);   /* ← GPIO 2 输出高电平，LED 亮 */
    break;

case ACT_LIGHT_OFF:
    printf("💡 执行: 关闭灯\n");
    gpio_ctrl_led_set(false);  /* ← GPIO 2 输出低电平，LED 灭 */
    break;

case ACT_FAN_ON:
    printf("🌀 执行: 打开风扇\n");
    gpio_ctrl_fan_set(true);   /* ← GPIO 7 输出高电平，风扇转 */
    break;

case ACT_FAN_OFF:
    printf("🌀 执行: 关闭风扇\n");
    gpio_ctrl_fan_set(false);  /* ← GPIO 7 输出低电平，风扇停 */
    break;
```

**gpio_ctrl.c** — 底层 GPIO 操作：

```c
/* LED 控制 */
void gpio_ctrl_led_set(bool on)
{
    s_led_state = on;
    gpio_set_level(GPIO_LED_PIN, on ? 1 : 0);
}

/* 风扇控制 */
void gpio_ctrl_fan_set(bool on)
{
    s_fan_state = on;
    gpio_set_level(GPIO_FAN_PIN, on ? 1 : 0);
}
```

---

## 4. 如何扩展新设备

### 示例：添加继电器控制风扇

**第一步：在 `gpio_ctrl.h` 中添加引脚定义和接口**

```c
#define GPIO_RELAY_FAN_PIN  7   /* 风扇继电器引脚 */

void gpio_ctrl_fan_set(bool on);
```

**第二步：在 `gpio_ctrl.c` 中实现**

```c
void gpio_ctrl_fan_set(bool on)
{
    gpio_set_level(GPIO_RELAY_FAN_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "风扇继电器 %s", on ? "ON" : "OFF");
}
```

别忘了在 `gpio_ctrl_init()` 中添加引脚配置：

```c
gpio_config_t fan_conf = {
    .pin_bit_mask = (1ULL << GPIO_RELAY_FAN_PIN),
    .mode = GPIO_MODE_OUTPUT,
    // ...
};
gpio_config(&fan_conf);
```

**第三步：在 `cmd_handler.c` 的 switch 中调用**

```c
case ACT_FAN_ON:
    printf("🌀 执行: 打开风扇\n");
    gpio_ctrl_fan_set(true);
    break;
```

**第四步：编译烧录**

```bash
idf.py build && idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

---

## 5. 引脚使用情况

### ESP32-S3-DevKitC-1 (N16R8) 引脚分配

| GPIO | 用途 | 状态 |
|:---:|------|:---:|
| 0 | Boot 按键 | ⚠️ 保留 |
| 1 | 可用 | 🟢 空闲 |
| **2** | **LED 灯** | 🔴 已占用 |
| 3 | 可用 | 🟢 空闲 |
| **4** | **I2S SCK (INMP441)** | 🔴 已占用 |
| **5** | **I2S WS (INMP441)** | 🔴 已占用 |
| **6** | **I2S SD (INMP441)** | 🔴 已占用 |
| **7** | **风扇控制** | 🔴 已占用 |
| 8~21 | 可用 | 🟢 空闲 |
| 26~37 | Flash/PSRAM (N16R8) | ❌ 不可用 |
| 38~48 | 可用 | 🟢 空闲 |

### 推荐扩展引脚

| 设备 | 推荐引脚 | 说明 |
|------|:---:|------|
| 继电器（风扇） | GPIO 7 | 靠近 LED，接线方便 |
| 继电器（空调） | GPIO 8 | — |
| 蜂鸣器 | GPIO 9 | 唤醒时提示音 |
| RGB LED (WS2812) | GPIO 48 | DevKitC-1 板载 RGB |
| 红外发射 | GPIO 10 | 控制空调遥控 |

---

## 6. 编译烧录

```bash
# 编译
idf.py build

# 烧录并监控串口输出
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 预期输出

```
I (xxx) gpio_ctrl: GPIO 初始化完成 — LED: GPIO2, 风扇: GPIO7
...
┌─── 注册内置命令词: 313 成功, 0 失败 ───┐
│ 注册自定义命令词: 19 成功, 0 失败
└─── 总计: 332 条命令词 ───┘
============ detect start ============
说出唤醒词 "嗨，乐鑫" 以激活命令词识别...
```

说 "嗨，乐鑫" → "打开灯" → LED 亮 ✅  
说 "嗨，乐鑫" → "关灯" → LED 灭 ✅  
说 "嗨，乐鑫" → "打开风扇" → 风扇转 ✅  
说 "嗨，乐鑫" → "关闭风扇" → 风扇停 ✅

---

## 总结

| 之前 | 现在 |
|------|------|
| 识别到命令只是 printf | 识别到命令 → 实际控制 GPIO |
| 所有代码在 main.c (780行) | 拆分 3 个文件，各司其职 |
| 添加新设备要改一大坨代码 | 只需改 gpio_ctrl + cmd_handler 对应位置 |
| 只支持 LED | 已支持 LED (GPIO2) + 风扇 (GPIO7) |

---

> 📖 相关文档：
> - `README-5-自定义命令词.md` — 命令词注册与踩坑记录
> - `README-1-驱动优化-INMP441.md` — INMP441 硬件驱动
> - `README-6-TODO-能力路线图.md` — 项目能力全景与 TODO
