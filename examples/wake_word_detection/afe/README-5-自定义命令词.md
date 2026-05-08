# 自定义命令词 — 踩坑记录与最终方案

> 记录从"能不能自定义命令词"到最终实现的完整过程，包括走过的弯路和解决方案。
> 对应代码文件：`main/main.c`

---

## 目录

1. [最初的问题](#1-最初的问题)
2. [第一次尝试：只注册 25 条自定义命令（失败）](#2-第一次尝试只注册-25-条自定义命令失败)
3. [第二次尝试：保留 313 条 + action_map 过滤（部分成功）](#3-第二次尝试保留-313-条--action_map-过滤部分成功)
4. [隐藏 BUG：mn_result->string 前导空格](#4-隐藏-bugmn_result-string-前导空格)
5. [第三次尝试：313 内置 + 自定义追加（最终方案 ✅）](#5-第三次尝试313-内置--自定义追加最终方案-)
6. [最终结果](#6-最终结果)
7. [唤醒词能自定义吗？](#7-唤醒词能自定义吗)
8. [如何添加新的自定义命令词](#8-如何添加新的自定义命令词)
9. [关键代码结构](#9-关键代码结构)

---

## 1. 最初的问题

> "命令词可以自己任意自定义吗？比如 `da kai you yan ji`（打开油烟机）？  
> 唤醒词可以自定义吗？比如 `嗨，韩梅梅`？"

**答案：**
- **命令词** → ✅ 可以自定义！任意有效拼音组合都能注册
- **唤醒词** → ❌ 不能自定义！必须使用乐鑫预训练的模型（约 50+ 种）

---

## 2. 第一次尝试：只注册 25 条自定义命令（失败）

### 做法

```c
esp_mn_commands_alloc(multinet, model_data);
esp_mn_commands_clear();  // 清空模型内置的 313 条

// 只添加 25 条自己想要的
esp_mn_commands_add(0, "da kai deng");
esp_mn_commands_add(1, "guan bi deng");
// ... 共 25 条

esp_mn_commands_update();  // 重建 FST
```

### 结果：置信度崩溃

```
┌─── 命令词识别成功 ───┐
│ command_id=0, 词= da kai deng, 置信度=0.22   ← 废了
└──────────────────────┘
⚠️  置信度过低 (0.22)，已忽略
```

| 命令词数量 | 置信度范围 | 状态 |
|:---:|:---:|:---:|
| 25 条 | 0.15 ~ 0.40 | ❌ 完全不可用 |
| 313 条 | 0.85 ~ 1.00 | ✅ 正常 |

### 根因分析

**MultiNet7 使用 RNNT/CTC 解码器 + FST (有限状态转换器) 搜索图。**

FST 搜索图的工作方式：
- 模型将语音解码为音素序列
- FST 图中每个命令词是一条"路径"
- 模型选择得分最高的路径，置信度 = 最优路径与其他路径的对比

**当路径太少（<50 条）时：**
- FST 中的音素覆盖太稀疏
- 模型对任何输入都无法给出高置信度（因为没有足够的"对比路径"）
- 这是模型内部数学机制，不是代码 bug

### 教训

> ⚠️ MultiNet7 不适合极少量命令词。需要 200+ 条命令词才能维持正常置信度。

---

## 3. 第二次尝试：保留 313 条 + action_map 过滤（部分成功）

### 思路

既然不能减少命令词，那就保留全部 313 条，在代码层面用 `action_map[]` 筛选：

```c
// 不调用任何 command 管理 API，让模型用内置 313 条
// 只在识别结果中匹配我们关心的命令

static const action_entry_t action_map[] = {
    { "da kai dian deng",  "打开电灯",  ACT_LIGHT_ON },
    { "da kai kong tiao",  "打开空调",  ACT_AC_ON    },
    // ... 只映射你关心的
};
```

### 问题

1. **只能用 313 条内置命令中已有的拼音**，不能自定义
   - 内置没有 "da kai deng"（只有 "da kai dian deng"）
   - 内置没有 "bo fang yin yue"（只有 "kai shi bo fang"）
   - 内置没有 "da kai feng shan"（只有 "da kai feng ji"）

2. **用户必须背住内置命令的精确说法**——体验很差

### 教训

> action_map 只能过滤，不能添加新命令。用户说的词必须在 313 条中有精确匹配。

---

## 4. 隐藏 BUG：mn_result->string 前导空格

### 现象

明明 action_map 中有 `"da kai dian deng"`，但 `strcmp` 总是不匹配：

```
┌─── 命令词识别 ───┐
│ 词= da kai dian deng, 置信度=0.94   ← 注意 "词=" 后面有空格!
└──────────────────┘
-----------继续聆听...-----------       ← 没有 ✅ 输出!
```

### 根因

`mn_result->string` 返回的字符串**带前导空格**：`" da kai dian deng"`（注意开头的空格）

代码中 `strcmp(" da kai dian deng", "da kai dian deng")` → 不相等！

### 修复

```c
static void execute_command(const char *pinyin, float confidence)
{
    /* 跳过模型返回字符串中的前导空格 */
    while (*pinyin == ' ') pinyin++;

    for (int i = 0; i < ACTION_MAP_SIZE; i++) {
        if (strcmp(pinyin, action_map[i].pinyin) == 0) {
            // 匹配成功，执行动作
        }
    }
}
```

### 教训

> ⚠️ MultiNet7 返回的 `mn_result->string` 带前导空格！比较前必须 trim。

---

## 5. 第三次尝试：313 内置 + 自定义追加（最终方案 ✅）

### 核心思路

```
既然模型需要 200+ 条命令词维持置信度，
那我就：
  ① 把 313 条内置命令全部重新注册回去
  ② 在此基础上追加我的自定义命令
  ③ 总计 332 条 → FST 正常 → 置信度正常 → 自定义命令也能识别
```

### 实现

```c
static void register_all_commands(multinet, model_data)
{
    esp_mn_commands_alloc(multinet, model_data);

    // ① 注册全部 313 条内置命令 (ID 1~313)
    for (int i = 0; i < 313; i++) {
        esp_mn_commands_add(i + 1, builtin_commands[i]);
    }

    // ② 追加自定义命令 (ID 400+)
    esp_mn_commands_add(400, "da kai deng");
    esp_mn_commands_add(400, "kai deng");
    esp_mn_commands_add(402, "da kai feng shan");
    esp_mn_commands_add(404, "bo fang yin yue");
    // ...

    // ③ 重建 FST 搜索图
    esp_mn_commands_update();
}
```

### 为什么需要重新注册 313 条？

`esp_mn_commands_update_from_sdkconfig()` 对 MultiNet7 **直接 return NULL**（什么都不做）。
313 条命令词是模型 `create()` 时内部加载的。一旦调用 `esp_mn_commands_alloc()` + `update()`，
模型的 FST 会被**完全替换**为你链表中的内容。所以必须把 313 条重新加进去。

---

## 6. 最终结果

```
┌─── 注册内置命令词: 313 成功, 0 失败 ───┐
│ 注册自定义命令词: 19 成功, 0 失败
└─── 总计: 332 条命令词 ───┘
332 active speech commands:
```

**识别效果：**

| 命令 | 类型 | 置信度 | 说明 |
|------|:---:|:---:|------|
| "打开风扇" `da kai feng shan` | 自定义 | **0.95** | 🟢 内置没有，自定义成功 |
| "打开灯" `da kai deng` | 自定义 | **0.72** | 🟢 内置没有，自定义成功 |
| "打开电灯" `da kai dian deng` | 内置 | **0.70** | 🟢 内置命令仍然正常 |
| "播放音乐" `bo fang yin yue` | 自定义 | 0.25 | 🟡 能识别，置信度偏低 |

### 三种方案对比

| 方案 | "打开灯" | "打开风扇" | 能否自定义 |
|------|:---:|:---:|:---:|
| ❌ 方案1: 只用 25 条自定义 | 0.22 | 0.19 | ✅ 但置信度废了 |
| ⚠️ 方案2: 313 内置 + 过滤 | 不存在 | 不存在 | ❌ 只能用内置的 |
| ✅ **方案3: 313 + 自定义** | **0.72** | **0.95** | ✅ 完美 |

---

## 7. 唤醒词能自定义吗？

**不能。** 唤醒词需要预训练的神经网络模型，无法通过 API 自定义。

| 对比 | 命令词 | 唤醒词 |
|------|--------|--------|
| 自定义 | ✅ `esp_mn_commands_add()` | ❌ 需要训练模型 |
| 灵活度 | 任意拼音组合 | 只能用预训练的 50+ 种 |
| 添加方式 | 代码中添加一行 | 需要联系乐鑫定制或使用现有 |
| 工作原理 | FST 搜索图匹配 | WakeNet 神经网络端到端检测 |

**可用的唤醒词示例**（完整列表见 `README-4-模型加载-唤醒词命令词.md`）：
- 中文："嗨，乐鑫"、"你好小智"、"小爱同学" 等
- 英文："Hi ESP"、"Alexa" 等

---

## 8. 如何添加新的自定义命令词

### 步骤 1：在 `custom_commands[]` 中添加拼音

```c
static const custom_cmd_t custom_commands[] = {
    // ... 已有的 ...
    { 411, "guan bi chuang lian", "关闭窗帘" },

    /* ── 新增：打开油烟机 ── */
    { 420, "da kai you yan ji",   "打开油烟机" },
    { 421, "guan bi you yan ji",  "关闭油烟机" },
};
```

### 步骤 2：在 `action_map[]` 中添加动作映射

```c
static const action_entry_t action_map[] = {
    // ... 已有的 ...
    { "da kai you yan ji",   "打开油烟机",  ACT_HOOD_ON  },
    { "guan bi you yan ji",  "关闭油烟机",  ACT_HOOD_OFF },
};
```

### 步骤 3：在 `execute_command()` 中添加处理

```c
case ACT_HOOD_ON:   printf("🍳 执行: 打开油烟机\n"); break;
case ACT_HOOD_OFF:  printf("🍳 执行: 关闭油烟机\n"); break;
```

### 步骤 4：编译烧录

```bash
idf.py build && idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 拼音规则

| 规则 | 示例 |
|------|------|
| 全小写 | ✅ `da kai deng` ❌ `Da Kai Deng` |
| 空格分隔音节 | ✅ `da kai deng` ❌ `dakaideng` |
| 无声调 | ✅ `da kai` ❌ `da4 kai1` |
| 无标点 | ✅ `da kai deng` ❌ `da kai, deng` |

### 什么拼音会被拒绝？

`esp_mn_commands_add()` 内部会调用 `check_speech_command()` 验证拼音。
如果拼音无法被解析为有效音素序列，注册会失败。启动时会打印：
```
│ ⚠️ 自定义命令注册失败: xxx (标签)
```

---

## 9. 关键代码结构

```
main/main.c
├── builtin_commands[313]          ← 内置命令词拼音（从模型中提取）
├── custom_commands[]              ← 你的自定义命令词
├── action_map[]                   ← 拼音→动作映射表
├── register_all_commands()        ← alloc + add(313) + add(custom) + update
├── execute_command()              ← strcmp 匹配 + switch 分发
├── feed_Task()                    ← 麦克风 → AFE
├── detect_Task()                  ← 唤醒词 + 命令词识别
└── app_main()                     ← 初始化
```

### 数据流

```
用户说话 → INMP441 麦克风 → I2S → feed_Task → AFE 引擎
                                                    │
                                            ┌───────┴───────┐
                                            │  唤醒词检测     │
                                            │ "嗨，乐鑫"     │
                                            └───────┬───────┘
                                                    │ 唤醒成功
                                                    ▼
                                            ┌───────────────┐
                                            │  MultiNet 识别  │
                                            │  332 条 FST    │
                                            └───────┬───────┘
                                                    │ string="da kai deng"
                                                    ▼
                                            ┌───────────────┐
                                            │ execute_command │
                                            │ action_map匹配 │
                                            └───────┬───────┘
                                                    │
                                                    ▼
                                            💡 执行: 打开灯
```

---

## 总结：关键教训

| # | 教训 | 说明 |
|---|------|------|
| 1 | MultiNet7 需要大量命令词 | <50 条时置信度会崩到 0.15~0.40，无法使用 |
| 2 | `esp_mn_commands_update()` 会替换整个 FST | 必须把 313 条全部重新注册 |
| 3 | `mn_result->string` 有前导空格 | 比较前必须 `while (*p == ' ') p++` |
| 4 | `esp_mn_commands_update_from_sdkconfig()` 对 MN7 无效 | 直接 return NULL |
| 5 | 313 条内置命令来自模型文件 | `create()` 时自动加载，不依赖 sdkconfig |
| 6 | 自定义命令可以任意拼音 | 只要能通过 `check_speech_command()` 验证 |
| 7 | 唤醒词不能自定义 | 需要预训练神经网络模型 |

---

> 📖 相关文档：
> - `README-4-模型加载-唤醒词命令词.md` — 模型加载机制与可用唤醒词列表
> - `README-1-驱动优化-INMP441.md` — INMP441 硬件驱动
> - `README-2-构建流程-从零开始.md` — 完整编译烧录步骤
> - [ESP-SR 官方文档](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/speech_command_recognition/README.html)
