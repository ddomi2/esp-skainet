# README-10 — 英文唤醒词 + 英文命令词版

> 当前运行路径固定使用英文唤醒词 `wn9_hiesp` 和英文命令模型 `mn7_en`。当前工程配置已关闭 `mn7_cn`，模型分区不再打包中文命令模型；同时命令窗口的状态机已经对齐仓库里的官方英文示例，不再额外手动关闭 WakeNet。

---

## 1. 功能概述

| 项目 | 说明 |
|------|------|
| 硬件 | ESP32-S3-DevKitC-1 + INMP441 麦克风 |
| 唤醒词 | "Hi ESP"（wn9_hiesp 模型） |
| 命令词 | mn7_en 模型内置 49 条英文命令 |
| 控制设备 | LED (GPIO 2) / Fan (GPIO 7) / Buzzer (GPIO 9) |
| 工作流程 | 说 "Hi ESP" → 6 秒内说一条英文命令 → 执行硬件操作 |

### 使用要点

1. 每次唤醒后只说 **一条** 英文命令，不要把 `turn on the light and sing a song` 合成一句。
2. 命令必须尽量贴近内置短语，例如 `turn on the light`、`sing a song`。
3. 当前工程已关闭 `mn7_cn`，构建和烧录只保留英文命令模型。

---

## 2. 文件结构

```
main/
├── CMakeLists.txt      ← 注册所有 .c 文件
├── main.c              ← 入口：feed_Task + detect_Task + app_main
├── cmd_handler.h/c     ← 命令 ID → 硬件动作映射（纯英文，无 FST 注册）
├── gpio_ctrl.h/c       ← 统一 GPIO 初始化入口
├── gpio_led.h/c        ← LED 灯控制（GPIO 2）
├── gpio_fan.h/c        ← 风扇控制（GPIO 7）
└── gpio_buzzer.h/c     ← 蜂鸣器 PWM 控制（GPIO 9）
```

---

## 3. mn7_en 内置命令词表（49 条）

mn7_en 模型在 `create()` 时自动加载以下命令，**不需要也不能手动注册**。

| ID | 英文命令 | 音素 | 映射动作 |
|----|---------|------|---------|
| 1 | tell me a joke | TfL Mm c qbK | — |
| 2 | sing a song | Sgl c Sel | 🔊 Buzzer ON |
| 3 | play news channel | PLd NoZ paNcL | 🔊 Buzzer ON |
| 4 | turn on my soundbox | TkN nN Mi StNDBnKS | 🔊 Buzzer ON |
| 5 | turn off my soundbox | TkN eF Mi StNDBnKS | 🔊 Buzzer OFF |
| 6 | highest volume | hicST VnLYoM | 🔊 Volume UP |
| 7 | lowest volume | LbcST VnLYoM | 🔉 Volume DOWN |
| 8 | increase the volume | gNKRmS jc VnLYoM | 🔊 Volume UP |
| 9 | decrease the volume | DgKRmS jc VnLYoM | 🔉 Volume DOWN |
| 10 | turn on the TV | TkN nN jc TmVm | — |
| 11 | turn off the TV | TkN eF jc TmVm | — |
| 12 | make me a tea | MdK Mm c Tm | — |
| 13 | make me a coffee | MdK Mm c KnFm | — |
| **14** | **turn on the light** | TkN nN jc LiT | **💡 LED ON** |
| **15** | **turn off the light** | TkN eF jc LiT | **💡 LED OFF** |
| 16 | change color to red | pdNq jc KcLk To RfD | — |
| 17 | change color to green | pdNq jc KcLk To GRmN | — |
| **18** | **turn on all the lights** | TkN nN eL jc LiTS | **💡 LED ON** |
| **19** | **turn off all the lights** | TkN eF eL jc LiTS | **💡 LED OFF** |
| **20** | **turn on the air conditioner** | TkN nN jc fR KcNDgscNk | **🌀 Fan ON** |
| **21** | **turn off the air conditioner** | TkN eF jc fR KcNDgscNk | **🌀 Fan OFF** |
| 22 | set temperature to 16 degrees | SfT jc TfMPRcpk To SgKSTmN DgGRmZ | — |
| 23 | set temperature to 17 degrees | ... | — |
| 24~32 | set temperature to 18~26 degrees | ... | — |
| **33** | **lowest fan speed** | LbcST FaN SPmD | **🌀 Fan OFF** |
| **34** | **medium fan speed** | MmDmcM FaN SPmD | **🌀 Fan ON** |
| **35** | **highest fan speed** | hicST FaN SPmD | **🌀 Fan ON** |
| 36 | auto-adjust fan speed | eTb cqcST jc FaN SPmD | — |
| **37** | **decrease fan speed** | DgKRmS jc FaN SPmD | **🌀 Fan OFF** |
| **38** | **increase fan speed** | gNKRmS jc FaN SPmD | **🌀 Fan ON** |
| 39 | increase the temperature | gNKRmS jc TfMPRcpk | — |
| 40 | decrease the temperature | DgKRmS jc TfMPRcpk | — |
| 41 | cool mode | KoLgl MbD | — |
| 42 | heat mode | hmTgl MbD | — |
| 43 | ventilation mode | VfNTcLdscN MbD | — |
| 44 | dehumidify mode | DmhYoMgDcFi MbD | — |

> **加粗** = 已映射到硬件动作，`—` = 识别但不执行操作（仅打印）

---

## 4. 语音控制命令速查

### 💡 LED 灯（GPIO 2）
| 说 | 效果 |
|----|------|
| "Turn on the light" | LED 亮 |
| "Turn off the light" | LED 灭 |
| "Turn on all the lights" | LED 亮 |
| "Turn off all the lights" | LED 灭 |

### 🌀 风扇（GPIO 7）
| 说 | 效果 |
|----|------|
| "Highest fan speed" | 风扇开 |
| "Increase fan speed" | 风扇开 |
| "Medium fan speed" | 风扇开 |
| "Lowest fan speed" | 风扇关 |
| "Decrease fan speed" | 风扇关 |
| "Turn on the air conditioner" | 风扇开 |
| "Turn off the air conditioner" | 风扇关 |

### 🔊 蜂鸣器（GPIO 9）
| 说 | 效果 |
|----|------|
| "Turn on my soundbox" | 蜂鸣器开 |
| "Sing a song" | 蜂鸣器开 |
| "Turn off my soundbox" | 蜂鸣器关 |
| "Increase the volume" | 音量 +1 |
| "Decrease the volume" | 音量 -1 |

---

## 5. 为什么不注册自定义命令词？

### mn7 架构限制

mn7_en 使用 **RNNT + CTC** 混合解码架构。模型在 `create()` 时内部加载预训练的命令词 FST（有限状态转换器），这个 FST 与模型权重是**联合训练**的。

```
                  ┌──────────────┐
create(mn7_en) →  │ 加载模型权重  │ → 内置 FST (49 commands)
                  │ 构建解码器    │      ↓
                  └──────────────┘   detect() 使用
```

如果调用 `set_speech_commands()` 替换 FST：

```
set_speech_commands() → 替换内置 FST → 新 FST 与模型权重不匹配
                                        ↓
                                   detect() 永远超时 ❌
```

**对比 mn7_cn（中文模型）**：中文模型设计上支持通过 `set_speech_commands()` 注册自定义拼音命令（需保留 313 条内置命令维持解码器覆盖率）。英文模型目前没有这个机制。

### 结论

| 特性 | mn7_cn (中文) | mn7_en (英文) |
|------|--------------|--------------|
| 内置命令 | 313 条（可查询） | 49 条（固定） |
| 自定义命令 | ✅ 通过 add() + update() | ❌ 替换会破坏解码器 |
| 注册方式 | alloc → add → update | 不注册，直接用 create() 内置 |
| 动作映射 | 拼音文本匹配 | command_id 匹配 |

---

## 6. 构建与烧录

### 6.1 环境准备

```bash
. ~/esp/esp-idf-v5.5.3/export.sh
```

### 6.2 menuconfig 关键配置

```
(Top) → ESP Speech Recognition
  Chinese Speech Commands Model  → None
  English Speech Commands Model  → general english recognition (mn7_en)  ← 必须启用
  Load Multiple Wake Words (WakeNet9s) → wn9_hiesp  ← 英文唤醒词

(Top) → Audio Media HAL → Audio hardware board
  → ESP32-S3-DevKitC-1 (INMP441)
```

### 6.3 构建 & 烧录

```bash
idf.py build
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 6.4 预期启动日志

```
Model: mn7_en
Model: wn9_hiesp
Wake word model: wn9_hiesp
LED 初始化完成 (GPIO2)
风扇初始化完成 (GPIO7)
蜂鸣器初始化完成 (GPIO9, PWM 2000Hz)
MultiNet model: mn7_en
Command path: mn7_en built-in commands only
┌─── [English] mn7_en built-in commands ───┐
│ Model loads 49 commands internally.
│ Mapped 19 commands to hardware actions:
│  💡 Light: "turn on/off the light"
│  🌀 Fan:   "highest/lowest fan speed"
│  🔊 Buzz:  "turn on/off my soundbox"
│  🔉 Vol:   "increase/decrease volume"
└──────────────────────────────────────────┘
┌─── Supported English commands (44 unique phrases) ───┐
│  1. tell me a joke
│  2. sing a song                     -> Buzzer ON
│ 14. turn on the light               -> LED ON
│ 35. highest fan speed               -> Fan ON
│ ...
└───────────────────────────────────────────────────────┘
49 active speech commands:
...
============ detect start ============
Say "Hi ESP" to activate command recognition...
```

---

## 7. 调试日志说明

为方便判断“是代码/模型问题，还是收音问题”，`main.c` 在唤醒后的 6 秒监听窗口里增加了调试日志：

```text
[DBG] MultiNet chunk=512 samples (~32 ms), peak threshold=400
[DBG] listening=1024 ms, frames=32, feed_peak=1180, feed_avg=180, afe_peak=175, afe_avg=35, voiced=2
[DBG] MultiNet state=TIMEOUT after 6336 ms (frames=198, peak=460, voiced=2)
Timeout (6s), input: <none>
[DBG] Speech reached MultiNet, but it was too weak/short for a full command
```

字段含义：

| 字段 | 含义 |
|------|------|
| `feed_peak` | 送入 AFE 之前的 feed 侧峰值，用来判断麦克风前端采样是否足够强 |
| `feed_avg` | feed 侧平均绝对幅度 |
| `peak` | 当前音频块的峰值振幅，越大说明语音越强 |
| `avg` | 当前音频块的平均绝对幅度 |
| `voiced` | 达到“像语音”阈值的块数量 |
| `state` | MultiNet 最终状态，常见为 `DETECTED` / `TIMEOUT` |

超时诊断含义：

| 调试结论 | 说明 |
|------|------|
| `No speech-like audio reached MultiNet...` | 唤醒后基本没采到有效语音 |
| `Speech reached MultiNet, but it was too weak/short...` | 采到了语音，但强度或连续时长不够 |
| `Speech reached MultiNet, but no built-in English command matched` | 语音强度足够，但说法没有命中内置英文命令 |

> 从当前实测日志看，`Hi ESP` 之后的命令音量明显偏弱，问题更接近拾音/说话时机，而不是 `mn7_en` 或 AFE 英文路径本身不可用。

---

## 8. GPIO 引脚分配

| GPIO | 设备 | 方向 | 说明 |
|------|------|------|------|
| 2 | LED | 输出 | 高电平亮 |
| 4 | I2S SCK | 输出 | INMP441 时钟 |
| 5 | I2S WS | 输出 | INMP441 字选择 |
| 6 | I2S SD | 输入 | INMP441 数据 |
| 7 | Fan | 输出 | 高电平开 |
| 9 | Buzzer | 输出 | LEDC PWM 2000Hz |

---

## 9. 代码架构简图

```
┌─────────────────────────────────────────────┐
│                  app_main()                  │
│  esp_board_init → gpio_ctrl_init → AFE init  │
│      ↓                                ↓      │
│  feed_Task (CPU0)           detect_Task (CPU1)│
│  INMP441 → I2S → AFE.feed   AFE.fetch → ...  │
│                               ↓               │
│                    ┌─── Wake word? ───┐       │
│                    │ "Hi ESP" detected │       │
│                    └────────┬─────────┘       │
│                             ↓                 │
│                    ┌─── detect() ────┐        │
│                    │ 6s listening     │        │
│                    │ mn7_en built-in  │        │
│                    └───────┬─────────┘        │
│                            ↓                  │
│                   cmd_handler_execute()        │
│                   command_id → action_map      │
│                        ↓                      │
│               ┌────────┼────────┐             │
│          gpio_led  gpio_fan  gpio_buzzer      │
│          (GPIO2)   (GPIO7)   (GPIO9)          │
└─────────────────────────────────────────────┘
```

---

## 10. 扩展新设备

以添加 "继电器控制" 为例：

### Step 1: 创建 `gpio_relay.h/c`

```c
// gpio_relay.h
#pragma once
#include "esp_err.h"
esp_err_t gpio_relay_init(void);
void gpio_relay_set(bool on);
```

### Step 2: 在 `gpio_ctrl.h` 中引入

```c
#include "gpio_relay.h"
```

### Step 3: 在 `cmd_handler.c` 中添加映射

```c
// action_id 枚举
ACT_RELAY_ON,
ACT_RELAY_OFF,

// action_map[] 添加
{ 10, "Turn on the TV → Relay",  ACT_RELAY_ON  },
{ 11, "Turn off the TV → Relay", ACT_RELAY_OFF },

// execute switch 中添加
case ACT_RELAY_ON:  gpio_relay_set(true);  break;
case ACT_RELAY_OFF: gpio_relay_set(false); break;
```

> 复用 mn7_en 内置的 "turn on/off the TV" 命令来控制继电器。

---

## 11. 已知限制

| 限制 | 说明 |
|------|------|
| 不支持自定义英文命令 | mn7 架构限制，`set_speech_commands()` 会破坏内置 FST |
| 命令词固定 49 条 | 由 mn7_en 模型预训练决定，无法增减 |
| 每次唤醒只建议说一条命令 | 合并两条命令或中英混说通常不会命中内置短语 |
| 需要清晰英文发音 | 模型对口音敏感，建议用标准美式英语 |
| 唤醒词后 6 秒超时 | 模型 create() 参数固定 6000ms |
| 其他已打包模型不会参与当前命令识别 | 当前 `detect_Task()` 固定选择 `mn7_en` |
