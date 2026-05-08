# ESP-Skainet 语音模型加载与唤醒词/命令词 完整指南

> 本文档详细说明唤醒词(WakeNet)和命令词(MultiNet)的模型加载机制、配置方法、关键代码及后续开发路线。

---

## 目录

1. [系统架构总览](#1-系统架构总览)
2. [模型存储与加载机制](#2-模型存储与加载机制)
3. [唤醒词 (WakeNet) 详解](#3-唤醒词-wakenet-详解)
4. [命令词 (MultiNet) 详解](#4-命令词-multinet-详解)
5. [关键代码走读](#5-关键代码走读)
6. [313 条默认命令词来源](#6-313-条默认命令词来源)
7. [自定义命令词](#7-自定义命令词)
8. [识别结果处理 — 接下来怎么做？](#8-识别结果处理--接下来怎么做)
9. [进阶开发路线](#9-进阶开发路线)

---

## 1. 系统架构总览

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          ESP32-S3 芯片                                  │
│                                                                          │
│  ┌─────────┐    ┌──────────────┐    ┌──────────┐    ┌──────────────┐    │
│  │ INMP441 │──→│  I2S 驱动     │──→│ AFE 引擎 │──→│  WakeNet     │    │
│  │ 麦克风  │    │ bsp_board.c  │    │ 降噪/VAD │    │ 唤醒词检测   │    │
│  └─────────┘    └──────────────┘    └──────────┘    └──────┬───────┘    │
│                                                            │             │
│                                                     唤醒成功↓             │
│                                                    ┌──────────────┐     │
│                                                    │  MultiNet    │     │
│                                                    │ 命令词识别    │     │
│                                                    └──────┬───────┘     │
│                                                           │              │
│                                                    识别结果↓              │
│                                                   ┌───────────────┐     │
│                                                   │ 业务逻辑处理   │     │
│                                                   │ GPIO/MQTT/... │     │
│                                                   └───────────────┘     │
│                                                                          │
│  ┌────────────── Flash 分区 ──────────────┐                             │
│  │ factory (app)  │  model (spiffs 5168KB) │                             │
│  │ 0x010000       │  存放 WakeNet+MultiNet │                             │
│  └────────────────┴────────────────────────┘                             │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 模型存储与加载机制

### 2.1 模型在哪里？

模型以二进制文件形式存储在 Flash 的 **model 分区**中：

```csv
# partitions.csv
# Name,  Type, SubType, Offset,  Size
factory, app,  factory, 0x010000, 2500k
model,  data, spiffs,         , 5168K
```

- **model 分区** = 5168KB，紧跟在 app 分区之后
- 编译时，所有在 `sdkconfig` 中启用的模型会被打包写入此分区
- `idf.py flash` 时自动烧录到对应偏移地址

### 2.2 加载流程（运行时）

```
esp_srmodel_init("model")
       │
       ├─ 1. 挂载 "model" 分区（SPIFFS 格式）
       ├─ 2. 遍历分区中所有模型文件
       ├─ 3. 解析模型名称 & 元信息
       └─ 4. 返回 srmodel_list_t 结构体
              │
              ├── model_name[] = ["wn9_hilexin", "mn7_cn", ...]
              ├── model_info[] = ["wakeNet9_v1h24_嗨,乐鑫_3_...", ...]
              ├── num = 2  (本项目包含 2 个模型)
              └── model_data[]  → 模型二进制数据指针
```

**关键数据结构** (`model_path.h`)：

```c
typedef struct {
    char **model_name;        // 模型名称数组，如 "wn9_hilexin", "mn7_cn"
    char **model_info;        // 模型详细信息
    esp_partition_t *partition; // Flash 分区指针
    void *mmap_handle;        // 内存映射句柄
    int num;                  // 模型数量
    srmodel_data_t **model_data; // 模型数据
} srmodel_list_t;
```

### 2.3 模型筛选

加载后通过 `esp_srmodel_filter()` 按关键字查找特定模型：

```c
// 查找唤醒词模型（包含 "wn" 前缀的）
char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
// → 返回 "wn9_hilexin"

// 查找中文命令词模型（包含 "mn" 前缀 + "cn" 的）
char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
// → 返回 "mn7_cn"

// 查找英文命令词模型
char *mn_en = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
// → 返回 "mn7_en" 或 NULL（如果未启用英文模型）
```

---

## 3. 唤醒词 (WakeNet) 详解

### 3.1 什么是唤醒词？

唤醒词是触发设备开始"听命令"的关键短语，类似 iPhone 的 "Hey Siri"。

- 模型始终在后台运行，低功耗持续监听
- 检测到唤醒词后才激活命令词识别
- 当前项目使用 **"嗨，乐鑫"** (`wn9_hilexin`)

### 3.2 可用唤醒词完整列表

在 `menuconfig → ESP Speech Recognition → Load Multiple Wake Words` 中配置。

#### WakeNet9 标准版 (wn9)

| 唤醒词 | 配置名 | 语言 |
|--------|--------|------|
| **嗨，乐鑫** | `wn9_hilexin` | 中文 |
| **Hi，ESP** | `wn9_hiesp` | 英文 |
| **你好小智** | `wn9_nihaoxiaozhi_tts` | 中文 |
| **小爱同学** | `wn9_xiaoaitongxue` | 中文 |
| **你好喵伴** | `wn9_nihaomiaoban_tts2` | 中文 |
| **Alexa** | `wn9_alexa` | 英文 |
| **Jarvis** | `wn9_jarvis_tts` | 英文 |
| **Computer** | `wn9_computer_tts` | 英文 |
| **Hey, Willow** | `wn9_heywillow_tts` | 英文 |
| **Hi, M Five** | `wn9_himfive` | 英文 |
| **Sophia** | `wn9_sophia_tts` | 英文 |
| **Hey, Wanda** | `wn9_heywanda_tts` | 英文 |
| **Hi, Jolly** | `wn9_hijolly_tts2` | 英文 |
| **Hi, Fairy** | `wn9_hifairy_tts2` | 英文 |
| **Hey, Printer** | `wn9_heyprinter_tts` | 英文 |
| **Mycroft** | `wn9_mycroft_tts` | 英文 |
| **Hi, Joy** | `wn9_hijoy_tts` | 英文 |
| **Hi, Jason** | `wn9_hijason_tts2` | 英文 |
| **Astrolabe** | `wn9_astrolabe_tts` | 英文 |
| **Hey, Ily** | `wn9_heyily_tts2` | 英文 |
| **Blue Chip** | `wn9_bluechip_tts2` | 英文 |
| **Hi, Andy** | `wn9_hiandy_tts2` | 英文 |
| **Hey, Ivy** | `wn9_heyivy_tts2` | 英文 |
| **Hey, Kira** | `wn9_heykira_tts3` | 英文 |
| **小龙小龙** | `wn9_xiaolongxiaolong_tts` | 中文 |
| **Hi, 喵喵** | `wn9_himiaomiao_tts` | 中文 |
| **喵喵同学** | `wn9_miaomiaotongxue_tts` | 中文 |
| **你好小鑫** | `wn9_nihaoxiaoxin_tts` | 中文 |
| **小美同学** | `wn9_xiaomeitongxue_tts` | 中文 |
| **Hi, Lily/莉莉** | `wn9_hilili_tts` | 中英 |
| **Hi, Telly/泰力** | `wn9_hitelly_tts` | 中英 |
| **小滨小滨/小冰小冰** | `wn9_xiaobinxiaobin_tts` | 中文 |
| **Hi, 小巫** | `wn9_haixiaowu_tts` | 中文 |
| **小鸭小鸭** | `wn9_xiaoyaxiaoya_tts2` | 中文 |
| **璃奈板** | `wn9_linaiban_tts2` | 中文 |
| **小酥肉** | `wn9_xiaosurou_tts2` | 中文 |
| **小宇同学** | `wn9_xiaoyutongxue_tts2` | 中文 |
| **小明同学** | `wn9_xiaomingtongxue_tts2` | 中文 |
| **小康同学** | `wn9_xiaokangtongxue_tts2` | 中文 |
| **小箭小箭** | `wn9_xiaojianxiaojian_tts2` | 中文 |
| **小特小特** | `wn9_xiaotexiaote_tts2` | 中文 |
| **你好小益** | `wn9_nihaoxiaoyi_tts2` | 中文 |
| **你好百应** | `wn9_nihaobaiying_tts2` | 中文 |
| **你好东东** | `wn9_nihaodongdong_tts2` | 中文 |
| **Hi Wall-E/瓦力** | `wn9_hiwalle_tts2` | 中英 |
| **小鹿小鹿** | `wn9_xiaoluxiaolu_tts2` | 中文 |
| **你好小安** | `wn9_nihaoxiaoan_tts2` | 中文 |
| **你好小脉** | `wn9_ni3hao3xiao3mai4_tts2` | 中文 |
| **你好小瑞** | `wn9_ni3hao3xiao3rui4_tts3` | 中文 |
| **嗨小欧** | `wn9_hai1xiao3ou1_tts3` | 中文 |
| **小珈小珈** | `wn9_xiao3jia1xiao3jia1_tts3` | 中文 |
| **小峰小峰** | `wn9_xiao3feng1xiao3feng1_tts3` | 中文 |
| **嗨小象** | `wn9_hai1xiao3xiang4_tts3` | 中文 |
| **你好星宝** | `wn9l_ni3hao3xing1bao3_tts3` | 中文 |
| **こんにちは ESP** | `wn9l_ja_konnichihaesp_tts3` | 日文 |
| **Bonjour ESP** | `wn9l_fr_bonjouresp_tts3` | 法文 |
| **Hi, Stack Chan** | `wn9l_histackchan_tts3` | 英文 |
| **Hey, GiGi** | `wn9l_heygigi_tts3` | 英文 |

#### WakeNet9s 轻量版 (wn9s) — 更小模型，速度更快

| 唤醒词 | 配置名 |
|--------|--------|
| 嗨，乐鑫 | `wn9s_hilexin` |
| Hi，ESP | `wn9s_hiesp` |
| 你好小智 | `wn9s_nihaoxiaozhi` |
| Hi, Jason | `wn9s_hijason` |

### 3.3 如何更换唤醒词

**方法一：通过 menuconfig（推荐）**

```bash
idf.py menuconfig
```

路径：`ESP Speech Recognition → Load Multiple Wake Words (WakeNet9)`

勾选你想要的唤醒词（可以同时选多个）。

⚠️ **每增加一个唤醒词，Flash 和 RAM 占用都会增加**。

**方法二：通过 sdkconfig.defaults.esp32s3**

```ini
# 当前配置（单唤醒词）
CONFIG_SR_WN_WN9_HILEXIN=y

# 添加第二个唤醒词
CONFIG_SR_WN_WN9_HIESP=y

# 如果想换成 "你好小智"
# CONFIG_SR_WN_WN9_HILEXIN=y   ← 注释掉或删除
CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y
```

修改后必须重新执行：

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py menuconfig    # 选择 INMP441 板型
idf.py build
idf.py flash monitor -p /dev/cu.usbmodem5B8E0653461
```

### 3.4 多唤醒词

AFE 配置支持同时加载 **最多 2 个唤醒词模型**：

```c
// afe_config 中的两个唤醒词槽位
afe_config->wakenet_model_name    // 第一个唤醒词
afe_config->wakenet_model_name_2  // 第二个唤醒词（可选）
```

在代码中判断是哪个唤醒词被触发：

```c
if (res->wakeup_state == WAKENET_DETECTED) {
    // res->wakenet_model_index = 模型索引 (1 或 2)
    // res->wake_word_index    = 该模型中的词索引
    printf("触发的唤醒词: model=%d, word=%d\n",
           res->wakenet_model_index, res->wake_word_index);
}
```

---

## 4. 命令词 (MultiNet) 详解

### 4.1 什么是命令词？

命令词是唤醒后用户说出的操作指令（如"打开空调""关闭灯光"），由 **MultiNet** 模型离线识别。

- 当前使用 **MultiNet7 中文版** (`mn7_cn`)
- 支持 313 条预定义中文命令词
- 纯离线运行，无需网络

### 4.2 识别行为

| 特性 | 说明 |
|------|------|
| **每次识别** | 一次只能识别一条命令词 |
| **超时** | 唤醒后 6 秒内未说命令，回到等待唤醒状态 |
| **连续识别** | 识别成功后保持聆听，可继续说下一条命令 |
| **再次唤醒** | 超时后需重新说唤醒词 |
| **置信度** | 返回 0.0~1.0，越高越可靠 |

> ❗ **不能一句话说多条命令**  
> "打开灯，打开空调" ← 这样说不行，只会识别第一个  
> 正确做法：说 "打开灯" → 等识别 → 再说 "打开空调"

### 4.3 可用 MultiNet 模型

| 配置名 | 说明 | 芯片支持 |
|--------|------|---------|
| `mn7_cn` | 通用中文识别 (313 条) | ESP32-S3 / ESP32-P4 |
| `mn7_cn_ac` | 空调控制专用 | ESP32-S3 / ESP32-P4 |
| `mn7_en` | 通用英文识别 | ESP32-S3 / ESP32-P4 |
| `mn6_cn` | 中文识别 (旧版) | ESP32-S3 |
| `mn6_cn_ac` | 空调控制 (旧版) | ESP32-S3 |
| `mn6_en` | 英文识别 (旧版) | ESP32-S3 |
| `mn5q8_cn` | 中文识别 (量化) | ESP32-S3 |

---

## 5. 关键代码走读

### 5.1 模型初始化 — `app_main()` (main.c:193-228)

```c
void app_main()
{
    // ① 初始化 INMP441 麦克风硬件
    ESP_ERROR_CHECK(esp_board_init(16000, 1, 16));

    // ② 从 Flash "model" 分区加载所有模型
    models = esp_srmodel_init("model");

    // ③ 遍历打印已加载的模型
    if (models) {
        for (int i = 0; i < models->num; i++) {
            // 通过前缀判断模型类型
            if (strstr(models->model_name[i], ESP_WN_PREFIX))  // "wn" 开头 = 唤醒词
                printf("唤醒词模型: %s\n", models->model_name[i]);
            if (strstr(models->model_name[i], ESP_MN_PREFIX))  // "mn" 开头 = 命令词
                printf("命令词模型: %s\n", models->model_name[i]);
        }
    }

    // ④ 创建 AFE 配置（自动读取板型的输入格式 "MN"）
    afe_config_t *afe_config = afe_config_init(
        esp_get_input_format(),  // INMP441 = "MN" (1 Mic + 1 Null)
        models,                   // 传入所有模型
        AFE_TYPE_SR,              // 语音识别类型
        AFE_MODE_LOW_COST         // 低功耗模式
    );

    // ⑤ 创建 AFE 实例并启动任务
    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);

    // ⑥ 启动两个 FreeRTOS 任务
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8*1024, afe_data, 5, NULL, 0);    // CPU0: 喂音频
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8*1024, afe_data, 5, NULL, 1); // CPU1: 检测
}
```

### 5.2 命令词初始化 — `detect_Task()` (main.c:57-95)

```c
void detect_Task(void *arg)
{
    // ① 从已加载的模型中筛选中文 MultiNet 模型
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    // 如果 sdkconfig 没有启用 MultiNet，mn_name 为 NULL → 降级运行

    // ② 获取 MultiNet 接口并创建模型实例
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    //                                                         ^^^^
    //                                          超时时间: 6000ms = 6 秒

    // ③ 从 sdkconfig 加载命令词列表
    //    这一步读取所有 CONFIG_CN_SPEECH_COMMAND_IDx 配置项
    esp_mn_commands_update_from_sdkconfig(multinet, model_data);
    //    → 加载 313 条命令词到 MultiNet 引擎

    // ④ 打印当前激活的命令词列表
    multinet->print_active_speech_commands(model_data);
}
```

### 5.3 识别状态机 — `detect_Task()` (main.c:99-178)

```
         ┌───────────────────────────────────────┐
         │         等待唤醒词                      │
         │   afe_handle->fetch() 持续运行          │
         │   wakeup_state == WAKENET_DETECTED?    │
         └───────────────┬───────────────────────┘
                         │ 是
                         ▼
         ┌───────────────────────────────────────┐
         │       命令词识别阶段                    │
         │   multinet->detect(model_data, data)  │
         │                                        │
         │   ESP_MN_STATE_DETECTING → 继续喂数据  │
         │   ESP_MN_STATE_DETECTED  → 识别成功 ──→ 输出结果, 继续聆听
         │   ESP_MN_STATE_TIMEOUT   → 超时    ──→ 重新启用 WakeNet
         └───────────────────────────────────────┘
```

### 5.4 文件位置速查

| 文件 | 作用 |
|------|------|
| `main/main.c` | 主程序：初始化 + feed_Task + detect_Task |
| `components/hardware_driver/boards/esp32s3-devkitc-1/bsp_board.c` | INMP441 I2S 驱动 |
| `components/hardware_driver/boards/include/esp32_s3_devkitc_1_board.h` | INMP441 引脚和配置 |
| `sdkconfig.defaults.esp32s3` | 默认配置（唤醒词+命令词模型选择） |
| `partitions.csv` | Flash 分区表（model 分区 5168KB） |
| `managed_components/espressif__esp-sr/src/include/model_path.h` | 模型加载 API 定义 |
| `managed_components/espressif__esp-sr/src/include/esp_mn_speech_commands.h` | 命令词管理 API |
| `managed_components/espressif__esp-sr/src/include/esp_process_sdkconfig.h` | sdkconfig 命令词加载 |
| `managed_components/espressif__esp-sr/Kconfig.projbuild` | 所有可用唤醒词/模型配置 |

---

## 6. 313 条默认命令词来源

### 6.1 它们是哪来的？

这 313 条命令词 **是系统自带的**，来自 ESP-SR 组件的 `Kconfig.projbuild` 和内部配置文件。

当你选择 `CONFIG_SR_MN_CN_MULTINET7_QUANT=y` (mn7_cn 通用中文模型) 时，系统自动内置了一套完整的中文语音命令词集，涵盖：

| 类别 | 示例 | 数量(约) |
|------|------|---------|
| 空调控制 | 打开空调、关闭空调、制冷模式 | 40+ |
| 灯光控制 | 打开灯、关闭灯、调高亮度 | 20+ |
| 风扇控制 | 增大风速、减小风速、摇头 | 15+ |
| 家电控制 | 打开电视、关闭窗帘 | 30+ |
| 温度控制 | 升高一度、降低一度、设定温度 | 20+ |
| 音乐播放 | 播放音乐、暂停、上一首 | 15+ |
| 定时控制 | 一小时后关机、八小时后开机 | 20+ |
| 通用指令 | 小乐小乐、关闭电灯 | 其余 |

### 6.2 如何查看所有 313 条？

运行时会打印所有命令词：

```
313 active speech commands:
Command 1: ba xiao shi hou guan ji
Command 2: ba xiao shi hou kai ji
Command 3: bi kai wo chui
...
Command 313: xiao xin xiao xin
```

命令词以 **拼音** 显示（空格分隔音节），这是 MultiNet 内部的表示格式。

---

## 7. 自定义命令词

### 7.1 方法一：代码中动态添加（推荐用于自定义场景）

使用 `esp_mn_commands_*` API 替换 sdkconfig 默认命令词：

```c
// ====== 在 detect_Task 中，替换 esp_mn_commands_update_from_sdkconfig() ======

esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
model_iface_data_t *model_data = multinet->create(mn_name, 6000);

// 初始化命令词链表
esp_mn_commands_alloc(multinet, model_data);

// 清空默认命令词（可选，如果只想用自己的命令）
esp_mn_commands_clear();

// 添加自定义命令词
// 参数: (command_id, "拼音用空格分隔")
esp_mn_commands_add(0, "da kai deng");        // 打开灯 → command_id=0
esp_mn_commands_add(1, "guan bi deng");        // 关闭灯 → command_id=1
esp_mn_commands_add(2, "da kai kong tiao");    // 打开空调 → command_id=2
esp_mn_commands_add(3, "guan bi kong tiao");   // 关闭空调 → command_id=3
esp_mn_commands_add(4, "da kai feng shan");    // 打开风扇 → command_id=4
esp_mn_commands_add(5, "guan bi feng shan");   // 关闭风扇 → command_id=5
esp_mn_commands_add(6, "bo fang yin yue");     // 播放音乐 → command_id=6
esp_mn_commands_add(7, "zan ting yin yue");    // 暂停音乐 → command_id=7

// 同一个 command_id 可以有多个表述方式（同义词）
esp_mn_commands_add(0, "kai deng");            // "开灯" 也映射到 command_id=0
esp_mn_commands_add(1, "guan deng");           // "关灯" 也映射到 command_id=1

// 更新到 MultiNet 引擎（必须调用！）
esp_mn_error_t *err = esp_mn_commands_update();
if (err) {
    printf("以下命令词解析失败:\n");
    // 处理错误...
}

// 打印当前激活的命令词
multinet->print_active_speech_commands(model_data);
```

### 7.2 方法二：通过 menuconfig（适用于 mn5 等旧模型）

> ⚠️ 仅 `mn5q8_cn` 等旧模型支持通过 menuconfig 逐条配置命令词。
> `mn7_cn` 使用内置大词表，menuconfig 中无 "Add Chinese speech commands" 菜单。

```
idf.py menuconfig
→ ESP Speech Recognition
  → Add Chinese speech commands
    → ID0: "da kai kong tiao"
    → ID1: "guan bi kong tiao"
    → ...
```

### 7.3 拼音转换规则

MultiNet 使用 **拼音（空格分隔）** 作为命令词输入：

| 中文 | 拼音 |
|------|------|
| 打开灯 | `da kai deng` |
| 关闭空调 | `guan bi kong tiao` |
| 播放音乐 | `bo fang yin yue` |
| 增大风速 | `zeng da feng su` |
| 升高一度 | `sheng gao yi du` |

工具：可使用 `esp-sr/tool/multinet_g2p.py` 进行文字到音素的转换。

---

## 8. 识别结果处理 — 接下来怎么做？

### 8.1 当前状态

识别成功后，代码中有一个 **TODO 标记**（main.c:156-161）：

```c
if (mn_state == ESP_MN_STATE_DETECTED) {
    esp_mn_results_t *mn_result = multinet->get_results(model_data);
    // command_id = 识别到的命令 ID
    // prob       = 置信度 (0.0~1.0)
    // string     = 匹配的拼音文本

    /*
     * TODO: 在这里根据 command_id 执行对应动作
     */
}
```

### 8.2 实现动作分发（GPIO 控制示例）

```c
#include "driver/gpio.h"

#define GPIO_LED   48   // LED 引脚
#define GPIO_RELAY 47   // 继电器引脚（控制空调等）

// 初始化 GPIO（在 app_main 中调用）
void action_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED) | (1ULL << GPIO_RELAY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

// 根据 command_id 执行动作（在 detect_Task 中调用）
void execute_command(int command_id, float confidence)
{
    // 置信度低于 0.6 时忽略，避免误识别
    if (confidence < 0.6) {
        printf("置信度过低 (%.2f)，忽略\n", confidence);
        return;
    }

    switch (command_id) {
        case 0:  // 打开灯
            gpio_set_level(GPIO_LED, 1);
            printf("💡 灯已打开\n");
            break;

        case 1:  // 关闭灯
            gpio_set_level(GPIO_LED, 0);
            printf("💡 灯已关闭\n");
            break;

        case 2:  // 打开空调
            gpio_set_level(GPIO_RELAY, 1);
            printf("❄️ 空调已打开\n");
            break;

        case 3:  // 关闭空调
            gpio_set_level(GPIO_RELAY, 0);
            printf("❄️ 空调已关闭\n");
            break;

        default:
            printf("未处理的命令 ID: %d\n", command_id);
            break;
    }
}
```

然后在 `detect_Task` 中调用：

```c
if (mn_state == ESP_MN_STATE_DETECTED) {
    esp_mn_results_t *mn_result = multinet->get_results(model_data);
    // ... 打印结果 ...

    // 执行对应动作
    execute_command(mn_result->command_id[0], mn_result->prob[0]);
}
```

### 8.3 更高级的动作方案

| 方案 | 适用场景 | 复杂度 |
|------|---------|--------|
| **GPIO 控制** | LED、继电器、蜂鸣器 | ⭐ |
| **MQTT 发布** | 物联网平台集成（Home Assistant等） | ⭐⭐ |
| **HTTP 请求** | 调用云端 API、智能家居网关 | ⭐⭐ |
| **UART 串口** | 控制外部 MCU 或设备 | ⭐⭐ |
| **ESP-NOW** | ESP32 设备间无线通信 | ⭐⭐⭐ |
| **BLE** | 蓝牙控制手机 App 或 BLE 设备 | ⭐⭐⭐ |

---

## 9. 进阶开发路线

### 阶段 1：基础完善 ✅ 已完成

- [x] INMP441 麦克风驱动
- [x] WakeNet 唤醒词检测 ("嗨，乐鑫")
- [x] MultiNet 命令词识别 (313 条)
- [x] 基本状态机

### 阶段 2：自定义命令词 + 动作绑定

```
目标: 精简命令词为实际需要的 10~20 条，绑定 GPIO/MQTT 动作

步骤:
1. 替换 esp_mn_commands_update_from_sdkconfig() 为自定义命令列表
2. 实现 execute_command() 动作分发函数
3. 添加置信度阈值过滤
4. 添加 LED/蜂鸣器反馈（唤醒时亮灯、识别成功时闪烁）
```

### 阶段 3：更换/添加唤醒词

```
目标: 使用自定义唤醒词或多唤醒词

步骤:
1. menuconfig 中选择需要的唤醒词
2. 代码中根据 wake_word_index 区分不同唤醒词的行为
3. 测试唤醒率和误唤醒率
```

### 阶段 4：联网智能控制

```
目标: 接入 WiFi，通过 MQTT/HTTP 控制智能家居

步骤:
1. 添加 WiFi 连接模块
2. 接入 MQTT Broker（如 Mosquitto、EMQX）
3. 命令识别后发布 MQTT 消息
4. Home Assistant / Node-RED 接收并执行
```

### 阶段 5：云端 ASR + LLM（可选）

```
目标: 实现自然语言理解，不限于固定命令词

步骤:
1. 唤醒后录音 → WiFi 上传到 FastAPI 服务端
2. 服务端使用 Whisper/FunASR 进行 ASR 转文字
3. 文字送入 LLM (GPT/通义千问) 理解意图
4. 返回控制指令给 ESP32 执行

参考: README-afe-2.md 中的方案 B 架构图
```

---

## 附录：快速参考

### sdkconfig.defaults.esp32s3 当前配置

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_SR_WN_WN9_HILEXIN=y              # 唤醒词: 嗨，乐鑫
CONFIG_SR_MN_CN_MULTINET7_QUANT=y       # 命令词模型: mn7_cn (通用中文)
CONFIG_SR_VADN_VADNET1_MEDIUM=y         # VAD: 中等精度
CONFIG_SPIRAM=y                          # 启用 PSRAM
CONFIG_SPIRAM_MODE_OCT=y                 # Octal SPI PSRAM
```

### 常用 API 速查

```c
// ─── 模型加载 ───
srmodel_list_t *models = esp_srmodel_init("model");              // 加载所有模型
char *name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE); // 筛选模型
esp_srmodel_deinit(models);                                       // 释放模型

// ─── 命令词管理 ───
esp_mn_commands_alloc(multinet, model_data);  // 初始化命令词链表
esp_mn_commands_add(id, "pin yin");           // 添加命令词
esp_mn_commands_remove("pin yin");            // 删除命令词
esp_mn_commands_modify("old", "new");         // 修改命令词
esp_mn_commands_clear();                      // 清空所有命令词
esp_mn_commands_update();                     // 更新到引擎（必须调用）
esp_mn_commands_print();                      // 打印所有命令词

// ─── MultiNet 识别 ───
esp_mn_state_t state = multinet->detect(model_data, audio_data);
esp_mn_results_t *result = multinet->get_results(model_data);
// result->command_id[0] = 识别到的命令 ID
// result->prob[0]       = 置信度
// result->string        = 匹配的拼音
```

---

> 📖 相关文档：
> - `README-afe-1.md` — INMP441 驱动优化详解
> - `README-afe-2.md` — 离线 vs 云端方案对比
> - `README-afe-all.md` — 完整编译烧录步骤
> - `README-afe-todo.md` — 功能对比与 TODO 路线图
