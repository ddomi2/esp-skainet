# 自定义命令词实现指南

> 本文档说明如何用代码方式自定义命令词，替代系统默认的 313 条命令词。
> 对应代码文件：`main/main.c`

---

## 目录

1. [改动概述](#1-改动概述)
2. [拼音规则详解](#2-拼音规则详解)
3. [代码结构详解](#3-代码结构详解)
4. [命令词列表（当前 26 条）](#4-命令词列表当前-26-条)
5. [如何添加新命令词](#5-如何添加新命令词)
6. [同义词机制](#6-同义词机制)
7. [动作分发 execute_command()](#7-动作分发-execute_command)
8. [与之前版本的对比](#8-与之前版本的对比)
9. [编译烧录步骤](#9-编译烧录步骤)
10. [常见问题](#10-常见问题)

---

## 1. 改动概述

### 核心改动：一行代码的替换

```diff
- #include "esp_process_sdkconfig.h"
+ #include "esp_mn_speech_commands.h"
```

```diff
- // 从 sdkconfig 加载 313 条系统默认命令词
- esp_mn_commands_update_from_sdkconfig(multinet, model_data);

+ // 注册自定义命令词（仅 26 条，精准匹配实际需求）
+ register_custom_commands(multinet, model_data);
```

```diff
+ // 识别成功后执行对应动作
+ execute_command(mn_result->command_id[0], mn_result->string, mn_result->prob[0]);
```

### 变化对比

| 项目 | 之前（v1） | 现在（v2） |
|------|-----------|-----------|
| 命令词来源 | sdkconfig 系统默认 | 代码中手动定义 |
| 命令词数量 | 313 条 | 26 条（18 个 command_id） |
| 同义词支持 | ❌ | ✅ 同一 ID 绑定多种说法 |
| 动作执行 | ❌ 仅打印 TODO | ✅ execute_command() 分发 |
| 置信度过滤 | ❌ | ✅ < 0.5 自动忽略 |
| 可维护性 | 需修改 menuconfig | 修改代码数组即可 |

---

## 2. 拼音规则详解

### 基本规则

MultiNet 中文模型使用 **汉语拼音** 作为命令词输入，规则如下：

| 规则 | 说明 | 示例 |
|------|------|------|
| **空格分隔** | 每个汉字的拼音之间用空格分开 | "打开灯" → `"da kai deng"` |
| **小写字母** | 全部使用小写 | ✅ `"da kai"` ❌ `"Da Kai"` |
| **无声调** | 不需要标注声调数字 | ✅ `"da kai"` ❌ `"da4 kai1"` |
| **无标点** | 不要加逗号、句号等 | ✅ `"da kai deng"` ❌ `"da kai, deng"` |
| **音节准确** | 按标准拼音，不要用方言/缩写 | ✅ `"kong tiao"` ❌ `"kongtiao"` |

### 拼音转换示例

| 中文 | 拼音 | 说明 |
|------|------|------|
| 打开灯 | `da kai deng` | 3 个音节 |
| 关闭空调 | `guan bi kong tiao` | 4 个音节 |
| 升高一度 | `sheng gao yi du` | 4 个音节 |
| 播放音乐 | `bo fang yin yue` | 4 个音节 |
| 下一首 | `xia yi shou` | 3 个音节 |
| 大声一点 | `da sheng yi dian` | 4 个音节 |
| 打开窗帘 | `da kai chuang lian` | 4 个音节 |
| 开灯 | `kai deng` | 2 个音节（短命令也可以） |

### 特殊拼音注意

| 易错字 | 正确拼音 | 常见错误 |
|--------|---------|---------|
| 量 (liáng/liàng) | `liang` | `lian` |
| 帘 (lián) | `lian` | `lien` |
| 窗 (chuāng) | `chuang` | `cuang` |
| 乐 (yuè) | `yue` | `le`（此处读 yuè） |
| 扇 (shàn) | `shan` | `san` |
| 调 (tiáo) in 空调 | `tiao` | `diao` |

### 如何查拼音

1. **百度搜索**: 搜 "打开灯 拼音"
2. **Python**: `pip install pypinyin`，然后 `from pypinyin import pinyin, Style; pinyin('打开灯', style=Style.NORMAL)`
3. **ESP-SR 工具**: `esp-sr/tool/multinet_g2p.py`（生成音素，更精确）

---

## 3. 代码结构详解

### 3.1 文件改动总览

只修改了一个文件：`main/main.c`

```
main/main.c
├── #include "esp_mn_speech_commands.h"   ← 新增头文件
├── enum { CMD_LIGHT_ON, ... }           ← 新增：命令 ID 枚举
├── custom_cmd_t custom_commands[]       ← 新增：命令词表（拼音→ID 映射）
├── register_custom_commands()            ← 新增：注册函数
├── execute_command()                     ← 新增：动作分发函数
├── feed_Task()                           ← 未修改
├── detect_Task()                         ← 修改：调用 register_custom_commands
└── app_main()                            ← 未修改
```

### 3.2 核心数据结构

```c
/* 命令词条目 */
typedef struct {
    int command_id;       // 命令 ID（用于 switch/case 分发）
    const char *pinyin;   // 汉语拼音（空格分隔音节）
    const char *label;    // 中文标签（仅用于日志）
} custom_cmd_t;

/* 命令词表（静态数组，编译时确定） */
static const custom_cmd_t custom_commands[] = {
    { CMD_LIGHT_ON,  "da kai deng",  "打开灯" },
    { CMD_LIGHT_ON,  "kai deng",     "开灯"   },  // 同义词
    { CMD_LIGHT_OFF, "guan bi deng", "关闭灯" },
    // ...
};
```

### 3.3 注册流程

```c
static void register_custom_commands(multinet, model_data)
{
    // ① 初始化命令词链表
    esp_mn_commands_alloc(multinet, model_data);

    // ② 清空已有命令词
    esp_mn_commands_clear();

    // ③ 逐条添加自定义命令词
    for (每条命令) {
        esp_mn_commands_add(command_id, pinyin);
    }

    // ④ 编译为 FST 搜索图（必须调用！）
    esp_mn_commands_update();
}
```

> ⚠️ **esp_mn_commands_update() 必须调用**，否则 MultiNet 不会生效新命令词。

### 3.4 识别 → 动作执行流程

```
用户说 "打开灯"
    │
    ├─ MultiNet 识别 → mn_state == ESP_MN_STATE_DETECTED
    │
    ├─ mn_result->command_id[0] == 0  (CMD_LIGHT_ON)
    │  mn_result->string == "da kai deng"
    │  mn_result->prob[0] == 0.95
    │
    ├─ execute_command(0, "da kai deng", 0.95)
    │
    ├─ 置信度 0.95 > 0.5 → 通过
    │
    ├─ switch(0) → case CMD_LIGHT_ON:
    │   printf("💡 执行: 打开灯")
    │   // TODO: gpio_set_level(GPIO_LED, 1);
    │
    └─ 继续聆听下一条命令...
```

---

## 4. 命令词列表（当前 26 条）

| command_id | 枚举名 | 拼音 | 中文 | 说明 |
|:---:|--------|------|------|------|
| 0 | CMD_LIGHT_ON | `da kai deng` | 打开灯 | 主表述 |
| 0 | CMD_LIGHT_ON | `kai deng` | 开灯 | 同义词 |
| 1 | CMD_LIGHT_OFF | `guan bi deng` | 关闭灯 | 主表述 |
| 1 | CMD_LIGHT_OFF | `guan deng` | 关灯 | 同义词 |
| 2 | CMD_AC_ON | `da kai kong tiao` | 打开空调 | |
| 3 | CMD_AC_OFF | `guan bi kong tiao` | 关闭空调 | |
| 4 | CMD_TEMP_UP | `sheng gao yi du` | 升高一度 | 主表述 |
| 4 | CMD_TEMP_UP | `tiao gao yi du` | 调高一度 | 同义词 |
| 5 | CMD_TEMP_DOWN | `jiang di yi du` | 降低一度 | 主表述 |
| 5 | CMD_TEMP_DOWN | `tiao di yi du` | 调低一度 | 同义词 |
| 6 | CMD_FAN_ON | `da kai feng shan` | 打开风扇 | |
| 7 | CMD_FAN_OFF | `guan bi feng shan` | 关闭风扇 | |
| 8 | CMD_FAN_UP | `zeng da feng su` | 增大风速 | |
| 9 | CMD_FAN_DOWN | `jian xiao feng su` | 减小风速 | |
| 10 | CMD_MUSIC_PLAY | `bo fang yin yue` | 播放音乐 | |
| 11 | CMD_MUSIC_PAUSE | `zan ting yin yue` | 暂停音乐 | 主表述 |
| 11 | CMD_MUSIC_PAUSE | `zan ting` | 暂停 | 同义词 |
| 12 | CMD_MUSIC_NEXT | `xia yi shou` | 下一首 | |
| 13 | CMD_MUSIC_PREV | `shang yi shou` | 上一首 | |
| 14 | CMD_VOL_UP | `zeng da yin liang` | 增大音量 | 主表述 |
| 14 | CMD_VOL_UP | `da sheng yi dian` | 大声一点 | 同义词 |
| 15 | CMD_VOL_DOWN | `jian xiao yin liang` | 减小音量 | 主表述 |
| 15 | CMD_VOL_DOWN | `xiao sheng yi dian` | 小声一点 | 同义词 |
| 16 | CMD_CURTAIN_OPEN | `da kai chuang lian` | 打开窗帘 | |
| 17 | CMD_CURTAIN_CLOSE | `guan bi chuang lian` | 关闭窗帘 | |

> **18 个唯一 command_id，26 条拼音表述（含 8 组同义词）**

---

## 5. 如何添加新命令词

### 步骤一：定义 command_id

在 `enum` 中添加新的 ID：

```c
enum {
    // ... 已有的 ...
    CMD_CURTAIN_CLOSE = 17,

    // ── 新增 ──
    CMD_TV_ON         = 18,  // 打开电视
    CMD_TV_OFF        = 19,  // 关闭电视

    CMD_MAX
};
```

### 步骤二：添加命令词条目

在 `custom_commands[]` 数组末尾添加：

```c
static const custom_cmd_t custom_commands[] = {
    // ... 已有的 ...
    { CMD_CURTAIN_CLOSE, "guan bi chuang lian", "关闭窗帘" },

    /* ── 电视控制（新增） ── */
    { CMD_TV_ON,         "da kai dian shi",     "打开电视" },
    { CMD_TV_ON,         "kai dian shi",        "开电视"   },  // 同义词
    { CMD_TV_OFF,        "guan bi dian shi",    "关闭电视" },
    { CMD_TV_OFF,        "guan dian shi",       "关电视"   },  // 同义词
};
```

### 步骤三：添加动作处理

在 `execute_command()` 的 switch 中添加：

```c
case CMD_TV_ON:
    printf("📺 执行: 打开电视\n");
    // gpio_set_level(GPIO_TV_RELAY, 1);
    break;
case CMD_TV_OFF:
    printf("📺 执行: 关闭电视\n");
    // gpio_set_level(GPIO_TV_RELAY, 0);
    break;
```

### 步骤四：重新编译

```bash
idf.py build
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

> ⚠️ 自定义命令词的改动**只需要 `build` + `flash`**，不需要 `fullclean` 或 `set-target`。

---

## 6. 同义词机制

### 原理

同一个 `command_id` 可以绑定多条拼音。用户说任意一条，都返回相同的 `command_id`。

```c
// 这两条拼音都会触发 command_id=0
{ CMD_LIGHT_ON, "da kai deng", "打开灯" },   // 说 "打开灯" → id=0
{ CMD_LIGHT_ON, "kai deng",    "开灯"   },   // 说 "开灯"   → id=0
```

### 识别结果区分

虽然 `command_id` 相同，但 `phrase_id` 不同，可以知道用户说的是哪一种表述：

```
说 "打开灯" → command_id=0, phrase_id=0, string="da kai deng"
说 "开灯"   → command_id=0, phrase_id=1, string="kai deng"
```

### 同义词设计建议

| 场景 | 主表述 | 同义词1 | 同义词2 |
|------|--------|---------|---------|
| 开灯 | `da kai deng` | `kai deng` | `ba deng da kai` |
| 关灯 | `guan bi deng` | `guan deng` | `ba deng guan diao` |
| 增大音量 | `zeng da yin liang` | `da sheng yi dian` | `yin liang da yi dian` |

> 同义词越多，识别越灵活，但也可能增加误识别。建议每个 ID 不超过 3~4 条同义词。

---

## 7. 动作分发 execute_command()

### 当前实现（纯日志）

```c
static void execute_command(int command_id, const char *phrase, float confidence)
{
    if (confidence < 0.5) {
        printf("⚠️  置信度过低，已忽略\n");
        return;
    }

    switch (command_id) {
    case CMD_LIGHT_ON:
        printf("💡 执行: 打开灯\n");
        break;
    // ...
    }
}
```

### 实际项目中的替换方案

#### 方案 A：GPIO 直接控制

```c
#include "driver/gpio.h"
#define GPIO_LED 48

case CMD_LIGHT_ON:
    gpio_set_level(GPIO_LED, 1);
    break;
```

#### 方案 B：MQTT 发消息

```c
#include "mqtt_client.h"

case CMD_LIGHT_ON:
    esp_mqtt_client_publish(mqtt_client,
        "home/light/cmd", "ON", 0, 1, 0);
    break;
```

#### 方案 C：UART 发指令给外部 MCU

```c
case CMD_LIGHT_ON:
    uart_write_bytes(UART_NUM_1, "LIGHT_ON\n", 9);
    break;
```

---

## 8. 与之前版本的对比

### 代码 diff 总结

```diff
 头文件：
- #include "esp_process_sdkconfig.h"
+ #include "esp_mn_speech_commands.h"

 新增代码：
+ enum { CMD_LIGHT_ON = 0, ... CMD_MAX }     (~20 行)
+ custom_cmd_t custom_commands[]              (~30 行)
+ register_custom_commands()                  (~25 行)
+ execute_command()                            (~50 行)

 修改代码（detect_Task 内）：
- esp_mn_commands_update_from_sdkconfig(multinet, model_data);
+ register_custom_commands(multinet, model_data);

- /* TODO: 在这里根据 command_id 执行对应动作 */
+ execute_command(mn_result->command_id[0], mn_result->string, mn_result->prob[0]);
```

### 运行效果对比

**之前（313 条系统命令）：**

```
313 active speech commands:
Command 1: ba xiao shi hou guan ji
Command 2: ba xiao shi hou kai ji
... (省略 311 条)
```

**现在（26 条自定义命令）：**

```
┌─── 注册自定义命令词 (26 条) ───┐
│ ID= 0  da kai deng              打开灯
│ ID= 0  kai deng                 开灯
│ ID= 1  guan bi deng             关闭灯
│ ID= 1  guan deng                关灯
│ ID= 2  da kai kong tiao         打开空调
│ ... (共 26 条)
└──────────────────────────────────┘
26 active speech commands:
```

**识别成功后：**

```
┌─── 命令词识别成功 ───┐
│ TOP 1: command_id=0, phrase_id=0, 词=da kai deng, 置信度=0.98
└──────────────────────┘
💡 执行: 打开灯
-----------继续聆听...-----------
```

---

## 9. 编译烧录步骤

### 如果是首次编译（或切换过 target）

```bash
cd esp-skainet/examples/wake_word_detection/afe

# 1. 清除旧编译（可选，target 不变时不需要）
idf.py fullclean

# 2. 设置芯片（仅首次或切换芯片时需要）
idf.py set-target esp32s3

# 3. 选择 INMP441 板型（set-target 后必须做）
idf.py menuconfig
#   → Audio Media HAL → Audio hardware board
#   → 选择 "ESP32-S3-DevKitC-1 (INMP441)"

# 4. 编译
idf.py build

# 5. 烧录并监控
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 如果只修改了命令词（最常见）

```bash
# 修改 main/main.c 中的 custom_commands[] 数组后：
idf.py build && idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

> 只改代码不需要 `fullclean` 或 `set-target`，直接 `build + flash` 即可。

---

## 10. 常见问题

### Q1: 命令词必须用拼音吗？不能直接写中文？

**是的，中文模型必须用拼音。** MultiNet 内部用拼音音素进行匹配，不支持直接输入汉字。

英文模型可以直接写英文单词（如 `"turn on the light"`）。

### Q2: 拼音写错了会怎样？

`esp_mn_commands_update()` 会返回解析失败的列表。如果拼音完全无法解析（如拼写错误），该条命令不会被注册。运行时日志会显示 `"⚠️ 部分命令词解析失败"`。

### Q3: 最多能添加多少条命令词？

理论上没有硬限制。实际建议：
- **10~50 条**: 最佳识别率
- **50~200 条**: 可用，但识别精度略降
- **200+ 条**: 可能出现混淆，误识别率上升

### Q4: 为什么同一个 command_id 可以有多条拼音？

这是 MultiNet 的同义词功能。不同的人可能用不同的说法表达同一个意思：
- "打开灯" / "开灯" / "把灯打开" → 都是开灯

绑定到同一个 `command_id` 后，`execute_command()` 只需要处理一个 ID。

### Q5: 置信度阈值设多少合适？

| 阈值 | 效果 |
|------|------|
| 0.3 | 宽松，容易误识别 |
| **0.5** | 当前默认，平衡 |
| 0.7 | 严格，可能需要说得更清楚 |
| 0.9 | 非常严格，可能经常识别不到 |

建议先用 0.5 测试，根据实际环境调整。

### Q6: 如何同时支持中英文命令？

需要同时加载中文和英文 MultiNet 模型：

```ini
# sdkconfig.defaults.esp32s3
CONFIG_SR_MN_CN_MULTINET7_QUANT=y   # 中文
CONFIG_SR_MN_EN_MULTINET7_QUANT=y   # 英文
```

代码中需要创建两个 MultiNet 实例，分别处理。

### Q7: esp_mn_commands_add 和 esp_mn_commands_update_from_sdkconfig 能混用吗？

可以。你可以先调用 `esp_mn_commands_update_from_sdkconfig()` 加载默认命令词，然后用 `esp_mn_commands_add()` 追加自定义的。但如果不需要默认的 313 条，直接用 `esp_mn_commands_clear()` + `esp_mn_commands_add()` 更干净。

---

## 附录：完整 API 参考

```c
/* ── 命令词管理 API ── */

// 初始化命令词链表（必须最先调用）
esp_err_t esp_mn_commands_alloc(const esp_mn_iface_t *multinet, model_iface_data_t *model_data);

// 添加一条命令词（可多次调用同一 ID 实现同义词）
esp_err_t esp_mn_commands_add(int command_id, const char *pinyin_string);

// 删除一条命令词
esp_err_t esp_mn_commands_remove(const char *pinyin_string);

// 修改一条命令词的拼音
esp_err_t esp_mn_commands_modify(const char *old_pinyin, const char *new_pinyin);

// 清空所有命令词
esp_err_t esp_mn_commands_clear(void);

// 编译命令词列表为 FST 搜索图（add/remove/modify 后必须调用）
esp_mn_error_t *esp_mn_commands_update(void);

// 打印所有命令词
void esp_mn_commands_print(void);

// 释放命令词链表
esp_err_t esp_mn_commands_free(void);

// 从 sdkconfig 加载默认命令词（我们已替换为自定义方式）
esp_mn_error_t *esp_mn_commands_update_from_sdkconfig(
    const esp_mn_iface_t *multinet, model_iface_data_t *model_data);

// 根据 command_id 获取拼音字符串
char *esp_mn_commands_get_string(int command_id);
```

---

> 📖 相关文档：
> - `README-4-模型加载-唤醒词命令词.md` — 模型加载机制与唤醒词详解
> - `README-1-驱动优化-INMP441.md` — INMP441 驱动优化
> - `README-2-构建流程-从零开始.md` — 完整编译烧录步骤
> - [ESP-SR 官方文档：命令词识别](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/speech_command_recognition/README.html)
