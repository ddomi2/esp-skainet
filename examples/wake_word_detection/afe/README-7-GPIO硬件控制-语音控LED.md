# GPIO 硬件控制 — 语音控制 LED 开关

> 本文档说明如何通过语音命令实际控制 ESP32-S3 的 GPIO 引脚。
> 以 LED 开/关为例，演示从"语音识别"到"硬件动作"的完整链路。

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
│                │
│            GPIO4├───── INMP441 SCK
│            GPIO5├───── INMP441 WS
│            GPIO6├───── INMP441 SD
│                │
│            3.3V├───── INMP441 VDD
│             GND├───── INMP441 GND / LED (-)
└────────────────┘
```

### 材料清单

| 材料 | 数量 | 说明 |
|------|:---:|------|
| LED（任意颜色） | 1 | 建议用红色或绿色，容易观察 |
| 330Ω 电阻 | 1 | 限流保护，防止烧坏 LED |
| 杜邦线 | 2 | 连接 GPIO2 和 GND |

### 为什么选 GPIO 2？

- ✅ 未被 I2S 麦克风占用（I2S 用 4/5/6）
- ✅ 未被 Flash/PSRAM 占用（26~37 被占）
- ✅ 在 DevKitC-1 排针上可直接访问
- ✅ 支持推挽输出，可直驱 LED

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
用户说 "打开灯"
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
└────────┬───────────┘
         │
         ▼
┌────────────────────┐
│  cmd_handler_execute│  strcmp 匹配 action_map[]
│                    │  → 找到 ACT_LIGHT_ON
└────────┬───────────┘
         │
         ▼
┌────────────────────┐
│  gpio_ctrl_led_set │  GPIO 2 → HIGH → LED 亮
│       (true)       │
└────────────────────┘
```

### 关键代码片段

**cmd_handler.c** — 执行硬件动作的部分：

```c
case ACT_LIGHT_ON:
    printf("💡 执行: 打开灯\n");
    gpio_ctrl_led_set(true);   /* ← 实际控制 GPIO 输出高电平 */
    break;

case ACT_LIGHT_OFF:
    printf("💡 执行: 关闭灯\n");
    gpio_ctrl_led_set(false);  /* ← GPIO 输出低电平 */
    break;
```

**gpio_ctrl.c** — 底层 GPIO 操作：

```c
void gpio_ctrl_led_set(bool on)
{
    s_led_state = on;
    gpio_set_level(GPIO_LED_PIN, on ? 1 : 0);  /* 直接设置引脚电平 */
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
| 7~21 | 可用 | 🟢 空闲 |
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
I (xxx) gpio_ctrl: GPIO 初始化完成 — LED 引脚: GPIO2
...
┌─── 注册内置命令词: 313 成功, 0 失败 ───┐
│ 注册自定义命令词: 19 成功, 0 失败
└─── 总计: 332 条命令词 ───┘
============ detect start ============
说出唤醒词 "嗨，乐鑫" 以激活命令词识别...
```

说 "嗨，乐鑫" → "打开灯" → LED 亮 ✅  
说 "嗨，乐鑫" → "关灯" → LED 灭 ✅

---

## 总结

| 之前 | 现在 |
|------|------|
| 识别到命令只是 printf | 识别到命令 → 实际控制 GPIO |
| 所有代码在 main.c (780行) | 拆分 3 个文件，各司其职 |
| 添加新设备要改一大坨代码 | 只需改 gpio_ctrl + cmd_handler 对应位置 |

---

> 📖 相关文档：
> - `README-5-自定义命令词.md` — 命令词注册与踩坑记录
> - `README-1-驱动优化-INMP441.md` — INMP441 硬件驱动
> - `README-6-TODO-能力路线图.md` — 项目能力全景与 TODO
