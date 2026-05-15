# README-9 — 双语识别：中英文切换方案

## 1. 问题

当前代码只支持中文语音命令（mn7_cn 模型），如何添加英文识别？能否中英文同时保留并自动切换？

## 2. 结论

| 问题 | 答案 |
|------|------|
| 能否添加英文？ | ✅ 可以，ESP-SR 提供 mn7_en 英文模型 |
| 能否中英文同时识别？ | ❌ 不能同时运行，只能运行时切换 |
| 能否自动检测语言？ | ❌ 不能，需要手动触发切换 |

**根本原因**：ESP-SR 的 MultiNet 命令注册是全局单例（全局链表 + 全局模型句柄），同一时刻只能绑定一个模型。

---

## 3. 架构分析

### 3.1 ESP-SR 命令注册机制

```
esp_mn_commands_alloc(multinet, model_data)   ← 绑定全局模型（只能一个）
esp_mn_commands_add(id, string)               ← 添加命令到全局链表
esp_mn_commands_update()                      ← 重建 FST 搜索图
```

关键限制：`esp_mn_commands_add()` 内部有**编译时**判断：

```c
// 源码: managed_components/espressif__esp-sr/src/esp_mn_speech_commands.c
#ifdef CONFIG_SR_MN_EN_MULTINET7_QUANT
    // 如果启用了英文模型 → 输入字符串会被 flite_g2p() 转为音素
    char *phonemes = flite_g2p(string, 1);
    check_speech_command(model_data, phonemes);  // 用音素验证
#else
    // 如果没启用英文模型 → 直接用拼音字符串验证
    check_speech_command(model_data, string);    // 用拼音验证
#endif
```

**⚠️ 致命冲突**：如果同时启用 CN 和 EN 配置，`esp_mn_commands_add("da kai deng")` 会被
`flite_g2p()` 转换为英文音素垃圾，导致中文命令注册全部失败！

### 3.2 可用模型

| 模型 | 语言 | 大小 | 说明 |
|------|------|------|------|
| mn7_cn | 中文 | 2.6MB | 输入为拼音（如 "da kai deng"） |
| mn7_en | 英文 | 2.6MB | 输入为英文文本（自动 G2P 转音素） |
| fst | 通用 | 12KB | FST 搜索算法支持文件 |

### 3.3 当前 Flash 分区

```
# 当前 partitions.csv
factory, app,  factory, 0x010000, 2500k
model,  data, spiffs,         , 5168K     ← 目前只装了 mn7_cn(2.6MB)+唤醒词+VAD ≈ 3.1MB
```

两个模型共存需要 ~5.7MB → **需要扩容分区到 8MB**。

---

## 4. 三种方案对比

### 方案 A：运行时切换（推荐）⭐

| 项目 | 说明 |
|------|------|
| 原理 | 同一时刻只运行一个模型，通过语音命令动态切换 |
| 中文 → 英文 | 说 "切换英文" → 销毁 CN 模型 → 创建 EN 模型 → 注册英文命令 |
| 英文 → 中文 | 说 "switch chinese" → 销毁 EN 模型 → 创建 CN 模型 → 注册中文命令 |
| 优点 | 一个固件支持双语，用户体验好，RAM占用低 |
| 缺点 | 切换耗时 2~3 秒（模型加载），需要修改框架源码 |
| 额外 RAM | 无（同时只有一个模型在内存中） |
| Flash 需求 | 需要扩大 model 分区到 ~8MB |

### 方案 B：双固件（最简单）

| 项目 | 说明 |
|------|------|
| 原理 | 编译两个固件，一个中文版一个英文版 |
| 切换方式 | 重新烧录不同固件 |
| 优点 | 零代码复杂度，直接改 sdkconfig |
| 缺点 | 切换语言需要重新烧录，不方便 |
| Flash 需求 | 不变（每个固件只有一个模型） |

### 方案 C：纯英文（最快实现）

| 项目 | 说明 |
|------|------|
| 原理 | 放弃中文，只用英文模型 |
| 优点 | 改动最小，只需改 sdkconfig + 命令表 |
| 缺点 | 失去中文支持 |

---

## 5. 方案 A 实现详解（运行时切换）

### 5.1 需要修改的文件

```
partitions.csv                    ← 扩大 model 分区
sdkconfig                         ← 同时启用 CN + EN 模型
managed_components/.../esp_mn_speech_commands.c  ← 修补运行时语言检测
main/cmd_handler.h                ← 新增语言切换接口
main/cmd_handler.c                ← 新增英文命令 + 切换逻辑
main/main.c                       ← detect_Task 支持模型重建
```

### 5.2 步骤一：扩大 Flash 分区

```csv
# partitions.csv — 修改后
# Name,  Type, SubType, Offset,  Size
factory, app,  factory, 0x010000, 2500k
model,  data, spiffs,         , 8000K
```

> ESP32-S3 N16R8 共 16MB Flash，留足空间。

### 5.3 步骤二：sdkconfig 启用双模型

```
idf.py menuconfig
```

路径：`(Top) → ESP Speech Recognition → MultiNet`

```
Chinese Speech Commands Model:  [*] mn7_cn (multinet7 quantized)
English Speech Commands Model:  [*] mn7_en (multinet7 quantized)   ← 开启这个
```

这会让 `movemodel.py` 同时打包 mn7_cn + mn7_en 到 model 分区。

### 5.4 步骤三：修补 esp_mn_commands_add() — 运行时语言检测

**问题**：启用 EN 后，`esp_mn_commands_add()` 的 `#ifdef` 会对所有输入调用 `flite_g2p()`，
导致中文拼音被错误转换。

**修补**：将编译时判断改为运行时判断。

修改文件：`managed_components/espressif__esp-sr/src/esp_mn_speech_commands.c`

```c
// ═══ 修改前（第 108~121 行）═══
#ifdef CONFIG_SR_MN_EN_MULTINET7_QUANT
    char *phonemes = flite_g2p(string, 1);
    if (esp_mn_model_handle->check_speech_command(esp_mn_model_data, phonemes) == 0) {
        ESP_LOGE(TAG, "invalid command, please check format, %s (%s).\n", string, phonemes);
        return ESP_ERR_INVALID_STATE;
    }
#else
    if (esp_mn_model_handle->check_speech_command(esp_mn_model_data, string) == 0) {
        ESP_LOGE(TAG, "invalid command, please check format, %s.\n", string);
        return ESP_ERR_INVALID_STATE;
    }
#endif

// ═══ 修改后 — 运行时检测当前绑定模型的语言 ═══
    char *lang = esp_mn_model_handle->get_language(esp_mn_model_data);
    if (lang && strcmp(lang, ESP_MN_ENGLISH) == 0) {
        // 英文模型：先 G2P 转换，再用音素验证
        char *phonemes = flite_g2p(string, 1);
        if (esp_mn_model_handle->check_speech_command(esp_mn_model_data, phonemes) == 0) {
            ESP_LOGE(TAG, "invalid command: %s (%s)\n", string, phonemes);
            free(phonemes);
            return ESP_ERR_INVALID_STATE;
        }
        // 保存音素到 phrase
        esp_mn_phrase_t *phrase = esp_mn_phrase_alloc(command_id, string);
        if (phrase == NULL) { free(phonemes); return ESP_ERR_INVALID_STATE; }
        int phoneme_len = strlen(phonemes);
        phrase->phonemes = _esp_mn_calloc_(phoneme_len + 1, sizeof(char));
        memcpy(phrase->phonemes, phonemes, phoneme_len);
        phrase->phonemes[phoneme_len] = '\0';
        free(phonemes);
        // ... (后续添加到链表，参考原代码)
    } else {
        // 中文模型：直接用拼音验证
        if (esp_mn_model_handle->check_speech_command(esp_mn_model_data, string) == 0) {
            ESP_LOGE(TAG, "invalid command: %s\n", string);
            return ESP_ERR_INVALID_STATE;
        }
        // ... (后续添加到链表，参考原代码)
    }
```

> ⚠️ 这是对 managed_component 源码的修改，`idf.py fullclean` 不会还原它，
> 但如果重新拉取组件（删除 managed_components）会丢失。建议 git 跟踪此文件。

### 5.5 步骤四：英文命令定义

在 `cmd_handler.c` 中添加英文命令表：

```c
/* ═══ 英文命令词表 — 用于 mn7_en 模型 ═══ */
static const custom_cmd_t english_commands[] = {
    /* ── 灯光 ── */
    { 400, "turn on the light",   "Turn ON light"  },
    { 400, "turn on light",       "Turn ON light"  },
    { 401, "turn off the light",  "Turn OFF light" },
    { 401, "turn off light",      "Turn OFF light" },

    /* ── 风扇 ── */
    { 402, "turn on the fan",     "Turn ON fan"    },
    { 402, "turn on fan",         "Turn ON fan"    },
    { 403, "turn off the fan",    "Turn OFF fan"   },
    { 403, "turn off fan",        "Turn OFF fan"   },

    /* ── 音乐 ── */
    { 404, "play music",          "Play music"     },
    { 405, "stop music",          "Stop music"     },
    { 405, "pause music",         "Pause music"    },

    /* ── 音量 ── */
    { 408, "volume up",           "Volume UP"      },
    { 408, "louder",              "Louder"         },
    { 409, "volume down",         "Volume DOWN"    },
    { 409, "quieter",             "Quieter"        },

    /* ── 语言切换 ── */
    { 500, "switch chinese",      "→ 切换中文"     },
    { 500, "switch to chinese",   "→ 切换中文"     },
};
static const int ENGLISH_CMD_COUNT = sizeof(english_commands) / sizeof(english_commands[0]);
```

中文命令中添加切换命令：
```c
    /* ── 语言切换 ── */
    { 500, "qie huan ying wen",   "切换英文" },
```

### 5.6 步骤五：语言切换逻辑

```c
/* ═══ cmd_handler.h 新增 ═══ */
typedef enum {
    LANG_CHINESE = 0,
    LANG_ENGLISH = 1,
} language_t;

/* 切换语言：销毁旧模型 → 创建新模型 → 注册对应命令 */
void cmd_handler_switch_language(language_t lang,
                                 esp_mn_iface_t **multinet,
                                 model_iface_data_t **model_data,
                                 srmodel_list_t *models);

/* 获取当前语言 */
language_t cmd_handler_get_language(void);
```

```c
/* ═══ cmd_handler.c 实现 ═══ */
static language_t current_lang = LANG_CHINESE;

void cmd_handler_switch_language(language_t lang,
                                 esp_mn_iface_t **multinet,
                                 model_iface_data_t **model_data,
                                 srmodel_list_t *models)
{
    /* 1. 销毁当前模型 */
    if (*model_data) {
        (*multinet)->destroy(*model_data);
        *model_data = NULL;
    }
    esp_mn_commands_free();  // 释放命令链表

    /* 2. 查找目标语言模型 */
    const char *lang_filter = (lang == LANG_ENGLISH) ? ESP_MN_ENGLISH : ESP_MN_CHINESE;
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, lang_filter);
    if (!mn_name) {
        printf("❌ 未找到 %s 模型!\n", lang_filter);
        return;
    }

    /* 3. 创建新模型 */
    *multinet = esp_mn_handle_from_name(mn_name);
    *model_data = (*multinet)->create(mn_name, 6000);
    printf("🌐 切换到: %s (%s)\n", mn_name, lang == LANG_ENGLISH ? "English" : "中文");

    /* 4. 注册对应语言的命令 */
    esp_mn_commands_alloc(*multinet, *model_data);

    if (lang == LANG_ENGLISH) {
        // 英文命令（mn7_en 内无内置命令，直接注册自定义英文）
        for (int i = 0; i < ENGLISH_CMD_COUNT; i++) {
            esp_mn_commands_add(english_commands[i].id, english_commands[i].pinyin);
        }
    } else {
        // 中文：注册 313 内置 + 自定义
        for (int i = 0; i < BUILTIN_CMD_COUNT; i++) {
            esp_mn_commands_add(i + 1, builtin_commands[i]);
        }
        for (int i = 0; i < CUSTOM_CMD_COUNT; i++) {
            esp_mn_commands_add(custom_commands[i].id, custom_commands[i].pinyin);
        }
    }

    esp_mn_commands_update();  // 重建 FST
    (*multinet)->print_active_speech_commands(*model_data);
    current_lang = lang;
}
```

### 5.7 步骤六：detect_Task 中处理切换

```c
/* detect_Task 中识别到 command_id == 500 时触发切换 */
if (mn_result->command_id[0] == 500) {
    language_t target = (current_lang == LANG_CHINESE) ? LANG_ENGLISH : LANG_CHINESE;
    cmd_handler_switch_language(target, &multinet, &model_data, models);
    mu_chunksize = multinet->get_samp_chunksize(model_data);
    // 切换完成，重新进入唤醒等待
    afe_handle->enable_wakenet(afe_data);
    wakeup_flag = 0;
    continue;
}
```

---

## 6. 方案 B 实现（纯英文固件）

如果不需要运行时切换，最简单的方式：

### 6.1 修改 sdkconfig

```bash
idf.py menuconfig
# Chinese Speech Commands Model → None
# English Speech Commands Model → mn7_en (multinet7 quantized)
```

### 6.2 修改 main.c

```c
// 将 ESP_MN_CHINESE 改为 ESP_MN_ENGLISH
char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
```

### 6.3 修改 cmd_handler.c

将所有命令从中文拼音改为英文文本：

```c
static const custom_cmd_t custom_commands[] = {
    { 400, "turn on the light",   "Turn ON light"  },
    { 401, "turn off the light",  "Turn OFF light" },
    { 402, "turn on the fan",     "Turn ON fan"    },
    { 403, "turn off the fan",    "Turn OFF fan"   },
    ...
};
```

> ⚠️ 英文模型 mn7_en **没有内置 313 条命令**，不需要注册 `builtin_commands[]`。
> 直接注册你自己的英文命令即可（G2P 转换由 `esp_mn_commands_add()` 内部自动完成）。

---

## 7. 英文命令词格式规范

### 7.1 mn7_en 支持的输入格式

| 格式 | 示例 | 说明 |
|------|------|------|
| 英文单词 | "turn on light" | 自动经过 flite_g2p 转为音素 |
| 全小写 | "play music" | 建议全小写，避免大小写问题 |
| 空格分词 | "volume up" | 每个单词用空格分隔 |

### 7.2 G2P (Grapheme-to-Phoneme) 工作原理

```
输入文本: "turn on the light"
     ↓ flite_g2p()
音素序列: "TkN nN jc Lit"      ← 自动转换，不需要手动指定
     ↓ 模型匹配
识别结果: command_id=400
```

### 7.3 可用的 Python 工具（验证命令词）

```bash
cd managed_components/espressif__esp-sr/tool/
pip install g2p_en
python multinet_g2p.py -t "turn on the light;turn off the light"
```

输出会显示每个命令的音素映射，可提前验证命令词是否有效。

---

## 8. ⚠️ 重要注意事项：`idf.py set-target` 会重置配置

### 8.1 问题

执行 `idf.py set-target esp32s3` 会**清空 build 目录并重置 sdkconfig**，导致之前在 menuconfig 中设置的选项全部丢失！

### 8.2 受影响的配置项

每次 `set-target` 后，必须重新进入 `idf.py menuconfig` 手动设置以下选项：

```
(Top) → ESP Speech Recognition:

1. English Speech Commands Model → general english recognition (mn7_en)  ⚠️ 默认是 None！
2. Load Multiple Wake Words (WakeNet9) → 勾选 wn9_hiesp              ⚠️ 默认未勾选！
3. Load Multiple Wake Words (WakeNet9s) → 勾选 wn9s_hijason (如需)
```

### 8.3 验证方法

设置完后保存退出，检查 sdkconfig 是否包含以下行：

```bash
grep "CONFIG_SR_MN_EN_MULTINET7_QUANT\|CONFIG_SR_WN_WN9_HIESP" sdkconfig
```

期望输出：
```
CONFIG_SR_MN_EN_MULTINET7_QUANT=y
CONFIG_SR_WN_WN9_HIESP=y
```

### 8.4 如果忘记设置会怎样？

| 遗漏配置 | 后果 |
|----------|------|
| mn7_en 未启用 | 说"切换英文"时找不到英文模型，切换失败 |
| wn9_hiesp 未启用 | "Hi ESP" 唤醒词不可用，只能用"嗨乐鑫"唤醒 |

### 8.5 完整操作流程（每次 set-target 后）

```bash
# 1. set-target（会重置 sdkconfig）
idf.py set-target esp32s3

# 2. 重新配置 menuconfig
idf.py menuconfig
#    → ESP Speech Recognition
#    → English Speech Commands Model → mn7_en
#    → Load Multiple Wake Words (WakeNet9) → 勾选 wn9_hiesp
#    → 保存退出

# 3. 构建（会自动打包所有模型到 srmodels.bin）
idf.py build

# 4. 烧录
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 8.6 sdkconfig.defaults 永久保存配置

为避免每次手动设置，可将关键配置写入 `sdkconfig.defaults.esp32s3`：

```bash
# 已有内容不动，追加以下行：
echo "CONFIG_SR_MN_EN_MULTINET7_QUANT=y" >> sdkconfig.defaults.esp32s3
echo "CONFIG_SR_WN_WN9_HIESP=y" >> sdkconfig.defaults.esp32s3
```

> `sdkconfig.defaults.esp32s3` 会在 `set-target esp32s3` 时自动应用，这样就不用每次手动设置了。

---

## 9. 内存与性能影响

| 指标 | 仅中文 | 运行时切换 |
|------|--------|-----------|
| Flash 模型分区 | ~3.1MB | ~5.7MB |
| PSRAM 占用 | ~2.5MB (一个模型) | ~2.5MB (同一时刻一个) |
| 切换延迟 | — | 2~3 秒 |
| CPU 占用 | 不变 | 不变（切换瞬间 spike） |
| 分区需求 | 5168K | **8000K**（需要扩大） |

---

## 9. 唤醒词的语言问题

| 唤醒词 | 语言 | 说明 |
|--------|------|------|
| "嗨，乐鑫" (wn9_hilexin) | 中文 | 当前使用，中英模式都可用 |
| "Hi Lexin" | 英文发音 | 同一个模型，英文说法也能触发 |
| "Alexa" | 英文 | 需要加载 wn9_alexa 唤醒词模型 |

> **结论**：唤醒词 "嗨，乐鑫" 在切换到英文模式后仍然有效（AFE 唤醒词模型不变）。
> 如果需要英文唤醒词（如 "Alexa"），需要在 menuconfig 中额外配置 WakeNet 模型。

---

## 10. 完整实施清单

```
□ 1. 修改 partitions.csv → model 分区扩大到 8000K
□ 2. idf.py menuconfig → 启用 English mn7_en
□ 3. 修补 esp_mn_speech_commands.c → 运行时语言检测
□ 4. cmd_handler.c → 添加英文命令表 + 切换逻辑
□ 5. cmd_handler.h → 新增 language_t 枚举 + 接口声明
□ 6. main.c detect_Task → 处理切换命令（command_id=500）
□ 7. idf.py build → 验证编译通过
□ 8. idf.py flash monitor → 测试切换功能
```

---

## 11. FAQ

### Q1: 为什么不能同时运行两个模型？

ESP-SR 的命令注册接口（`esp_mn_commands_alloc/add/update`）使用模块级全局变量：
```c
static esp_mn_node_t *esp_mn_root = NULL;          // 全局命令链表
const static esp_mn_iface_t *esp_mn_model_handle;  // 全局模型句柄
static model_iface_data_t *esp_mn_model_data;      // 全局模型数据
```
同一时刻只能绑定一个模型。如果要双模型并行，需要完全绕过这套 helper 函数，
直接使用 `multinet->set_speech_commands()` 底层 API 构建两套独立链表 — 复杂度极高。

### Q2: 中英文自动切换（免声明）可行吗？

理论上可以同时 feed 音频给两个模型做检测，但：
- RAM 翻倍（两个模型同时在 PSRAM 中 ~5MB）
- CPU 翻倍（两个 detect 并行运算）
- ESP32-S3 双核可能勉强够用，但实时性和稳定性难保证

**结论**：不推荐。手动切换是最稳定的方案。

### Q3: mn7_en 有内置命令词吗？

**没有**。mn7_en 是一个通用英文语音识别模型（类似空白画布），
你需要自己注册所有命令词。不像 mn7_cn 有 313 条空调/家电内置命令。

### Q4: 英文识别准确率如何？

mn7_en 对简短命令识别率较高（2~4 个单词的短语），长句子准确率下降。
建议命令词设计：
- ✅ "turn on light" (3 词，清晰)
- ✅ "play music" (2 词，清晰)
- ❌ "please turn on the living room light" (7 词，太长)

### Q5: 修改了 managed_components 的文件，更新组件会丢失吗？

会。建议：
1. 将修改后的文件用 git 跟踪（已在工作区）
2. 或将修改内容提取为一个 patch 文件保存
3. 每次 `idf.py fullclean` 或重新 fetch 组件后需要重新应用

---

## 12. 实际实现状态 ✅ (已完成)

### 12.1 实现概要

方案 A（运行时切换）已完整实现并编译通过，零错误零警告。

#### 修改的文件清单

| 文件 | 修改内容 |
|------|----------|
| `partitions.csv` | model 分区 5168K → **8000K** |
| `sdkconfig` | 启用 `CONFIG_SR_MN_EN_MULTINET7_QUANT=y` + `CONFIG_SR_WN_WN9_HIESP=y` |
| `managed_components/.../esp_mn_speech_commands.c` | `#ifdef` → 运行时 `get_language()` 检测 |
| `main/cmd_handler.h` | 新增 `language_t` 枚举 + 切换/注册接口声明 |
| `main/cmd_handler.c` | 英文命令表 + 双语动作表 + 注册/切换/执行逻辑 |
| `main/main.c` | detect_Task 处理 ACT_LANG_SWITCH → 调用切换函数 |

### 12.2 唤醒词配置

| 唤醒词 | 模型 | 用于 |
|--------|------|------|
| "嗨，乐鑫" | wn9_hilexin | 中英文模式通用 |
| "Hi, ESP" | wn9_hiesp | 中英文模式通用（新增） |

> AFE 同时加载两个唤醒词模型，任一唤醒词均可激活命令词识别（与当前命令语言无关）。

### 12.3 英文命令词表

| 英文命令 | 功能 | 对应中文 |
|----------|------|----------|
| turn on the light / turn on light | 开灯 | da kai deng |
| turn off the light / turn off light | 关灯 | guan bi deng |
| turn on the fan / turn on fan | 开风扇 | da kai feng shan |
| turn off the fan / turn off fan | 关风扇 | guan bi feng shan |
| play music | 播放音乐 | bo fang yin yue |
| stop music / pause music | 暂停 | zan ting / guan bi yin yue |
| volume up / louder | 增大音量 | zeng da yin liang / da sheng yi dian |
| volume down / quieter | 减小音量 | jian xiao yin liang / xiao sheng yi dian |
| switch chinese / switch to chinese | 切换中文 | — |
| — | 切换英文 | qie huan ying wen |

### 12.4 语言切换流程

```
用户(中文模式): "嗨，乐鑫" → "切换英文"
  ↓
系统: 销毁 mn7_cn → 加载 mn7_en → 注册英文命令 → FST 重建
  ↓
用户(英文模式): "Hi ESP" → "turn on light"
  ↓
用户(英文模式): "Hi ESP" → "switch chinese"
  ↓
系统: 销毁 mn7_en → 加载 mn7_cn → 注册中文命令 → FST 重建
```

### 12.5 关键技术：框架补丁

`esp_mn_speech_commands.c` 第 108~121 行的修改是整个双语方案的核心：

```c
// ═══ 补丁：运行时语言检测（替换编译时 #ifdef）═══
char *lang = esp_mn_model_handle->get_language(esp_mn_model_data);
int is_english = (lang && strcmp(lang, ESP_MN_ENGLISH) == 0);

if (is_english) {
    // 英文模型 → flite_g2p 转音素 → 验证
    char *phonemes = flite_g2p(string, 1);
    check_speech_command(esp_mn_model_data, phonemes);
    ...
} else {
    // 中文模型 → 直接用拼音验证
    check_speech_command(esp_mn_model_data, string);
    ...
}
```

> ⚠️ **注意**：此补丁在 `managed_components/` 中，执行 `idf.py fullclean` 或重新拉取组件会丢失！
> 已被 git 跟踪，但务必注意组件更新风险。

### 12.6 测试步骤

```bash
# 1. 构建
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py build

# 2. 烧录 + 监控
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461

# 3. 测试中文模式
#    说: "嗨，乐鑫" → "打开灯" / "切换英文"

# 4. 测试英文模式 (切换后)
#    说: "Hi ESP" → "turn on light" / "switch chinese"
```

### 12.7 构建输出

```
Project build complete.
wake_word_detection.bin binary size 0x14ad60 bytes (1.36MB)
Smallest app partition is 0x271000 bytes. 47% free.
```

---

## 13. 参考

- [ESP-SR MultiNet 文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html)
- [mn7_en 英文示例](../../en_speech_commands_recognition/main/main.c)
- [flite_g2p 工具](managed_components/espressif__esp-sr/tool/multinet_g2p.py)
- 中文模型内置命令: `managed_components/espressif__esp-sr/model/multinet_model/mn7_cn/`
