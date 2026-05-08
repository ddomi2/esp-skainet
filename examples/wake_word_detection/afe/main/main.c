/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_board_init.h"
#include "model_path.h"
#include "string.h"

static const esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
static srmodel_list_t *models = NULL;

/*
 * ═══════════════════════════════════════════════════════════
 *  自定义命令词 ID 定义
 *
 *  command_id 是命令词的唯一标识符，用于 execute_command() 中分发动作。
 *  同一个 command_id 可以绑定多个拼音表述（同义词），
 *  例如 CMD_LIGHT_ON 同时绑定 "da kai deng" 和 "kai deng"。
 * ═══════════════════════════════════════════════════════════
 */
enum {
    CMD_LIGHT_ON      = 0,   // 打开灯
    CMD_LIGHT_OFF     = 1,   // 关闭灯
    CMD_AC_ON         = 2,   // 打开空调
    CMD_AC_OFF        = 3,   // 关闭空调
    CMD_TEMP_UP       = 4,   // 升高温度
    CMD_TEMP_DOWN     = 5,   // 降低温度
    CMD_FAN_ON        = 6,   // 打开风扇
    CMD_FAN_OFF       = 7,   // 关闭风扇
    CMD_FAN_UP        = 8,   // 增大风速
    CMD_FAN_DOWN      = 9,   // 减小风速
    CMD_MUSIC_PLAY    = 10,  // 播放音乐
    CMD_MUSIC_PAUSE   = 11,  // 暂停音乐
    CMD_MUSIC_NEXT    = 12,  // 下一首
    CMD_MUSIC_PREV    = 13,  // 上一首
    CMD_VOL_UP        = 14,  // 增大音量
    CMD_VOL_DOWN      = 15,  // 减小音量
    CMD_CURTAIN_OPEN  = 16,  // 打开窗帘
    CMD_CURTAIN_CLOSE = 17,  // 关闭窗帘
    CMD_MAX
};

/*
 * ═══════════════════════════════════════════════════════════
 *  命令词表：定义中文拼音与 command_id 的映射
 *
 *  【拼音规则】
 *  - 使用小写汉语拼音，每个音节之间用空格分隔
 *  - 不需要声调标记（模型自动处理）
 *  - 示例: "打开灯" → "da kai deng"，"关闭空调" → "guan bi kong tiao"
 *
 *  【同义词】
 *  - 同一个 command_id 可以添加多条拼音，识别任一条均返回该 id
 *  - 例如 "打开灯" 和 "开灯" 都映射到 CMD_LIGHT_ON
 * ═══════════════════════════════════════════════════════════
 */
typedef struct {
    int command_id;       // 命令 ID（对应 enum 中的值）
    const char *pinyin;   // 拼音（空格分隔音节）
    const char *label;    // 中文标签（仅用于日志显示）
} custom_cmd_t;

static const custom_cmd_t custom_commands[] = {
    /* ── 灯光控制 ── */
    { CMD_LIGHT_ON,      "da kai deng",         "打开灯"   },
    { CMD_LIGHT_ON,      "kai deng",            "开灯"     },  // 同义词
    { CMD_LIGHT_OFF,     "guan bi deng",        "关闭灯"   },
    { CMD_LIGHT_OFF,     "guan deng",           "关灯"     },  // 同义词

    /* ── 空调控制 ── */
    { CMD_AC_ON,         "da kai kong tiao",    "打开空调" },
    { CMD_AC_OFF,        "guan bi kong tiao",   "关闭空调" },
    { CMD_TEMP_UP,       "sheng gao yi du",     "升高一度" },
    { CMD_TEMP_UP,       "tiao gao yi du",      "调高一度" },  // 同义词
    { CMD_TEMP_DOWN,     "jiang di yi du",       "降低一度" },
    { CMD_TEMP_DOWN,     "tiao di yi du",        "调低一度" },  // 同义词

    /* ── 风扇控制 ── */
    { CMD_FAN_ON,        "da kai feng shan",    "打开风扇" },
    { CMD_FAN_OFF,       "guan bi feng shan",   "关闭风扇" },
    { CMD_FAN_UP,        "zeng da feng su",     "增大风速" },
    { CMD_FAN_DOWN,      "jian xiao feng su",   "减小风速" },

    /* ── 音乐控制 ── */
    { CMD_MUSIC_PLAY,    "bo fang yin yue",     "播放音乐" },
    { CMD_MUSIC_PAUSE,   "zan ting yin yue",    "暂停音乐" },
    { CMD_MUSIC_PAUSE,   "zan ting",            "暂停"     },  // 同义词
    { CMD_MUSIC_NEXT,    "xia yi shou",         "下一首"   },
    { CMD_MUSIC_PREV,    "shang yi shou",       "上一首"   },
    { CMD_VOL_UP,        "zeng da yin liang",   "增大音量" },
    { CMD_VOL_UP,        "da sheng yi dian",    "大声一点" },  // 同义词
    { CMD_VOL_DOWN,      "jian xiao yin liang", "减小音量" },
    { CMD_VOL_DOWN,      "xiao sheng yi dian",  "小声一点" },  // 同义词

    /* ── 窗帘控制 ── */
    { CMD_CURTAIN_OPEN,  "da kai chuang lian",  "打开窗帘" },
    { CMD_CURTAIN_CLOSE, "guan bi chuang lian", "关闭窗帘" },
};

static const int CUSTOM_CMD_COUNT = sizeof(custom_commands) / sizeof(custom_commands[0]);

/*
 * ═══════════════════════════════════════════════════════════
 *  register_custom_commands: 注册自定义命令词到 MultiNet
 *
 *  替代 esp_mn_commands_update_from_sdkconfig()，使用代码方式
 *  注册自定义命令词列表，不再依赖 sdkconfig 中的 313 条默认词。
 * ═══════════════════════════════════════════════════════════
 */
static void register_custom_commands(const esp_mn_iface_t *multinet, model_iface_data_t *model_data)
{
    /* 初始化命令词链表（必须在 add 之前调用） */
    esp_mn_commands_alloc(multinet, model_data);

    /* 清空默认命令词（如果有的话） */
    esp_mn_commands_clear();

    /* 逐条注册自定义命令词 */
    printf("┌─── 注册自定义命令词 (%d 条) ───┐\n", CUSTOM_CMD_COUNT);
    for (int i = 0; i < CUSTOM_CMD_COUNT; i++) {
        esp_err_t ret = esp_mn_commands_add(
            custom_commands[i].command_id,
            custom_commands[i].pinyin
        );
        if (ret == ESP_OK) {
            printf("│ ID=%2d  %-24s  %s\n",
                   custom_commands[i].command_id,
                   custom_commands[i].pinyin,
                   custom_commands[i].label);
        } else {
            printf("│ ❌ 注册失败: %s (%s)\n",
                   custom_commands[i].pinyin,
                   custom_commands[i].label);
        }
    }
    printf("└──────────────────────────────────┘\n");

    /*
     * 更新到 MultiNet 引擎（必须调用！）
     * 此函数将命令词列表编译为 FST 搜索图，用于实时识别。
     * 如果某些拼音无法解析，返回错误链表。
     */
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err) {
        printf("⚠️  部分命令词解析失败（拼音可能有误）\n");
    }
}

/*
 * ═══════════════════════════════════════════════════════════
 *  execute_command: 根据 command_id 执行对应动作
 *
 *  目前仅打印日志，实际项目中可替换为：
 *  - GPIO 控制（LED、继电器）
 *  - MQTT 消息发布
 *  - UART 发送指令给外部设备
 *  - ESP-NOW 无线通信
 * ═══════════════════════════════════════════════════════════
 */
static void execute_command(int command_id, const char *phrase, float confidence)
{
    /* 置信度低于 0.5 时忽略，避免误识别 */
    if (confidence < 0.5) {
        printf("⚠️  置信度过低 (%.2f)，已忽略: %s\n", confidence, phrase);
        return;
    }

    switch (command_id) {
    /* ── 灯光控制 ── */
    case CMD_LIGHT_ON:
        printf("💡 执行: 打开灯\n");
        // TODO: gpio_set_level(GPIO_LED, 1);
        break;
    case CMD_LIGHT_OFF:
        printf("💡 执行: 关闭灯\n");
        // TODO: gpio_set_level(GPIO_LED, 0);
        break;

    /* ── 空调控制 ── */
    case CMD_AC_ON:
        printf("❄️  执行: 打开空调\n");
        break;
    case CMD_AC_OFF:
        printf("❄️  执行: 关闭空调\n");
        break;
    case CMD_TEMP_UP:
        printf("🌡️ 执行: 温度+1°C\n");
        break;
    case CMD_TEMP_DOWN:
        printf("🌡️ 执行: 温度-1°C\n");
        break;

    /* ── 风扇控制 ── */
    case CMD_FAN_ON:
        printf("🌀 执行: 打开风扇\n");
        break;
    case CMD_FAN_OFF:
        printf("🌀 执行: 关闭风扇\n");
        break;
    case CMD_FAN_UP:
        printf("🌀 执行: 风速+1\n");
        break;
    case CMD_FAN_DOWN:
        printf("🌀 执行: 风速-1\n");
        break;

    /* ── 音乐控制 ── */
    case CMD_MUSIC_PLAY:
        printf("🎵 执行: 播放音乐\n");
        break;
    case CMD_MUSIC_PAUSE:
        printf("🎵 执行: 暂停音乐\n");
        break;
    case CMD_MUSIC_NEXT:
        printf("🎵 执行: 下一首\n");
        break;
    case CMD_MUSIC_PREV:
        printf("🎵 执行: 上一首\n");
        break;
    case CMD_VOL_UP:
        printf("🔊 执行: 音量+\n");
        break;
    case CMD_VOL_DOWN:
        printf("🔉 执行: 音量-\n");
        break;

    /* ── 窗帘控制 ── */
    case CMD_CURTAIN_OPEN:
        printf("🪟 执行: 打开窗帘\n");
        break;
    case CMD_CURTAIN_CLOSE:
        printf("🪟 执行: 关闭窗帘\n");
        break;

    default:
        printf("❓ 未知命令 ID: %d\n", command_id);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  feed_Task: 从麦克风读取音频数据，喂入 AFE 引擎
 * ═══════════════════════════════════════════════════════════ */
void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch == feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag) {
        esp_get_feed_data(true, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════
 *  detect_Task: 唤醒词检测 + MultiNet 命令词识别
 *
 *  状态机：
 *    等待唤醒 → 唤醒成功 → 命令词识别 → 识别成功/超时 → 回到等待唤醒
 * ═══════════════════════════════════════════════════════════ */
void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);

    /* ────── 初始化 MultiNet 命令词识别模型 ────── */
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (mn_name == NULL) {
        printf("错误: 未找到 MultiNet 命令词模型!\n");
        printf("请确认 sdkconfig 中已启用 CONFIG_SR_MN_CN_MULTINET7_QUANT=y\n");
        printf("然后重新执行: idf.py set-target esp32s3 && idf.py build\n");
        printf("当前仅运行唤醒词检测模式...\n");
        /* 降级为仅唤醒词检测 */
        while (task_flag) {
            afe_fetch_result_t* res = afe_handle->fetch(afe_data);
            if (!res || res->ret_value == ESP_FAIL) break;
            if (res->wakeup_state == WAKENET_DETECTED) {
                printf("唤醒词已检测到! (无命令词模型，无法继续识别)\n");
            }
        }
        vTaskDelete(NULL);
        return;
    }
    printf("multinet 模型: %s\n", mn_name);

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    /* 6000 = 命令词识别超时时间 (ms)，超过此时间未识别到命令则回到等待唤醒 */
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);

    /* 注册自定义命令词（替代 sdkconfig 中的 313 条默认命令） */
    register_custom_commands(multinet, model_data);

    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    /* 打印当前激活的命令词 */
    multinet->print_active_speech_commands(model_data);
    printf("============ detect start ============\n");
    printf("说出唤醒词 \"嗨，乐鑫\" 以激活命令词识别...\n");

    int wakeup_flag = 0;

    while (task_flag) {
        afe_fetch_result_t* res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            printf("fetch error!\n");
            break;
        }

        /* ────── 唤醒词检测 ────── */
        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("══════════════════════════════════\n");
            printf("  唤醒词已检测到!\n");
            printf("  model index:%d, word index:%d\n",
                   res->wakenet_model_index, res->wake_word_index);
            printf("══════════════════════════════════\n");
            /* 清空 MultiNet 内部状态，准备新一轮命令词识别 */
            multinet->clean(model_data);
        }

        /*
         * 根据通道数判断唤醒是否确认：
         * - 单通道 (INMP441): WAKENET_DETECTED 即可进入识别
         * - 多通道 (阵列麦): 需等待 WAKENET_CHANNEL_VERIFIED 确认方向
         */
        if (res->raw_data_channels == 1 && res->wakeup_state == WAKENET_DETECTED) {
            wakeup_flag = 1;
            printf("-----------正在聆听命令词...-----------\n");
        } else if (res->raw_data_channels > 1 && res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            printf("通道验证通过, channel: %d\n", res->trigger_channel_id);
            wakeup_flag = 1;
            printf("-----------正在聆听命令词...-----------\n");
        }

        /* ────── 命令词识别阶段 ────── */
        if (wakeup_flag == 1) {
            /* 将 AFE 处理后的音频数据送入 MultiNet 进行命令词识别 */
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                /* 正在识别中，继续喂数据 */
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                /* 识别成功：输出 TOP N 结果 */
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("┌─── 命令词识别成功 ───┐\n");
                for (int i = 0; i < mn_result->num; i++) {
                    printf("│ TOP %d: command_id=%d, phrase_id=%d, 词=%s, 置信度=%.2f\n",
                           i + 1,
                           mn_result->command_id[i],
                           mn_result->phrase_id[i],
                           mn_result->string,
                           mn_result->prob[i]);
                }
                printf("└──────────────────────┘\n");

                /* 执行识别到的命令 */
                execute_command(
                    mn_result->command_id[0],
                    mn_result->string,
                    mn_result->prob[0]
                );

                printf("-----------继续聆听...-----------\n");
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                /* 超时：用户唤醒后没有说命令词，回到等待唤醒状态 */
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("识别超时 (%.1f 秒未说命令词), 当前输入: %s\n",
                       6000 / 1000.0, mn_result->string);

                /* 重新启用唤醒词检测 */
                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;
                printf("-----------等待唤醒词...-----------\n");
                continue;
            }
        }
    }

    /* 清理 MultiNet 资源 */
    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    printf("detect task exit\n");
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════
 *  app_main: 初始化硬件、模型、AFE，启动任务
 * ═══════════════════════════════════════════════════════════ */
void app_main()
{
    ESP_ERROR_CHECK(esp_board_init(16000, 1, 16));

    /* 从 flash 分区加载所有 SR 模型（WakeNet + MultiNet） */
    models = esp_srmodel_init("model");
    if (models) {
        for (int i = 0; i < models->num; i++) {
            if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {
                printf("唤醒词模型: %s\n", models->model_name[i]);
            }
            if (strstr(models->model_name[i], ESP_MN_PREFIX) != NULL) {
                printf("命令词模型: %s\n", models->model_name[i]);
            }
        }
    }

    /* 初始化 AFE 配置 */
    afe_config_t *afe_config = afe_config_init(esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);

    if (afe_config->wakenet_model_name) {
        printf("AFE 唤醒词模型: %s\n", afe_config->wakenet_model_name);
    }
    if (afe_config->wakenet_model_name_2) {
        printf("AFE 唤醒词模型2: %s\n", afe_config->wakenet_model_name_2);
    }

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);
    /* detect_Task 内存加大到 8KB（MultiNet 需要更多栈空间） */
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void*)afe_data, 5, NULL, 1);
}
