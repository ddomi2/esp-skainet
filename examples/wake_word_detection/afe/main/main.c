/*
 * @file main.c
 * @brief 语音唤醒 + 命令词识别 + 硬件控制 — 主程序
 *
 * 本文件只负责：
 *   - 初始化硬件（音频 + GPIO）
 *   - 启动 AFE 音频前端引擎
 *   - 运行 feed_Task（麦克风数据采集）和 detect_Task（唤醒+识别）
 *
 * 命令词相关逻辑已拆分到：
 *   - cmd_handler.h/c — 命令注册 + 动作映射 + 执行
 *   - gpio_ctrl.h/c   — GPIO 硬件控制（LED 等）
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
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
#include "esp_board_init.h"
#include "model_path.h"
#include "string.h"

/* 引入拆分出的模块 */
#include "cmd_handler.h"
#include "gpio_ctrl.h"
#include "wifi_portal.h"

static const esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
static srmodel_list_t *models = NULL;

typedef struct {
    esp_afe_sr_data_t *afe_data;
    esp_mn_iface_t *multinet;
    model_iface_data_t *model_data;
} detect_task_ctx_t;

/* ═══════════════════════════════════════════════════════════
 *  feed_Task: 从 INMP441 麦克风读取音频数据，喂入 AFE 引擎
 *
 *  这是一个死循环任务，不断从 I2S 读取 PCM 数据并送入 AFE。
 *  AFE 内部会做 VAD（语音活动检测）+ 降噪处理。
 * ═══════════════════════════════════════════════════════════ */
void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch == feed_channel);

    /* 分配音频缓冲区（chunk大小 × 通道数 × 每样本2字节） */
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag) {
        /* 从 I2S 驱动读取一帧音频数据 */
        esp_get_feed_data(true, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        /* 送入 AFE 引擎处理 */
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
 *    等待唤醒 → 唤醒成功 → 聆听命令词 → 识别成功/超时 → 回到等待
 *
 *  识别成功后调用 cmd_handler_execute() 执行硬件操作。
 * ═══════════════════════════════════════════════════════════ */
void detect_Task(void *arg)
{
    detect_task_ctx_t *ctx = (detect_task_ctx_t *)arg;
    esp_afe_sr_data_t *afe_data = ctx->afe_data;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);

    if (ctx->multinet == NULL || ctx->model_data == NULL) {
        printf("错误: 未找到 MultiNet 命令词模型!\n");
        printf("请确认 sdkconfig 中已启用 CONFIG_SR_MN_CN_MULTINET7_QUANT=y\n");
        printf("然后重新执行: idf.py set-target esp32s3 && idf.py build\n");
        printf("当前仅运行唤醒词检测模式...\n");
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

    esp_mn_iface_t *multinet = ctx->multinet;
    model_iface_data_t *model_data = ctx->model_data;
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

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
            multinet->clean(model_data);  /* 清除之前的识别状态 */
        }

        /* 根据通道数进入命令词识别状态 */
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
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;  /* 还在识别中，等待下一帧 */
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                /* 识别成功：获取结果并执行动作 */
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("┌─── 命令词识别 ───┐\n");
                printf("│ 词=%s, 置信度=%.2f\n",
                       mn_result->string, mn_result->prob[0]);
                printf("└──────────────────┘\n");

                /* 通过拼音文本匹配动作表，执行硬件操作 */
                cmd_handler_execute(mn_result->string, mn_result->prob[0]);

                printf("-----------继续聆听...-----------\n");
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                /* 超时：6秒内未说出命令词，回到唤醒等待状态 */
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("识别超时 (6 秒), 输入: %s\n", mn_result->string);
                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;
                printf("-----------等待唤醒词...-----------\n");
                continue;
            }
        }
    }

    if (model_data) {
        multinet->destroy(model_data);
        ctx->model_data = NULL;
    }
    printf("detect task exit\n");
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════
 *  app_main: 程序入口 — 初始化硬件、模型、AFE，启动任务
 *
 *  执行顺序：
 *    1. 初始化音频硬件（I2S + INMP441）
 *    2. 初始化 GPIO（LED 引脚）
 *    3. 初始化 Wi-Fi（常驻 SoftAP 配网页 + STA 联网）
 *    4. 加载语音模型（唤醒词 + 命令词）
 *    5. 配置 AFE 音频前端
 *    6. 启动 feed_Task 和 detect_Task
 * ═══════════════════════════════════════════════════════════ */
void app_main()
{
    /* ── 1. 初始化音频硬件（16kHz 采样率，1通道，16bit） ── */
    ESP_ERROR_CHECK(esp_board_init(16000, 1, 16));

    /* ── 2. 初始化 GPIO 设备控制（LED 等） ── */
    ESP_ERROR_CHECK(gpio_ctrl_init());

    /* ── 3. 初始化 Wi-Fi：固定 AP + 网页配置 + STA 自动联网 ── */
    esp_err_t wifi_err = wifi_portal_init();
    if (wifi_err != ESP_OK) {
        printf("Wi-Fi 初始化失败，不影响当前离线语音功能: %s\n",
               esp_err_to_name(wifi_err));
    }

    /* ── 4. 加载语音模型 ── */
    models = esp_srmodel_init("model");
    if (models) {
        for (int i = 0; i < models->num; i++) {
            if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL)
                printf("唤醒词模型: %s\n", models->model_name[i]);
            if (strstr(models->model_name[i], ESP_MN_PREFIX) != NULL)
                printf("命令词模型: %s\n", models->model_name[i]);
        }
    }

    /* ── 5. 配置 AFE 音频前端引擎 ── */
    afe_config_t *afe_config = afe_config_init(
        esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);

    if (afe_config->wakenet_model_name)
        printf("AFE 唤醒词模型: %s\n", afe_config->wakenet_model_name);
    if (afe_config->wakenet_model_name_2)
        printf("AFE 唤醒词模型2: %s\n", afe_config->wakenet_model_name_2);

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    detect_task_ctx_t *detect_ctx = calloc(1, sizeof(detect_task_ctx_t));
    assert(detect_ctx);
    detect_ctx->afe_data = afe_data;

    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (mn_name != NULL) {
        printf("multinet 模型: %s\n", mn_name);
        detect_ctx->multinet = esp_mn_handle_from_name(mn_name);
        detect_ctx->model_data = detect_ctx->multinet->create(mn_name, 6000);
        cmd_handler_register(detect_ctx->multinet, detect_ctx->model_data);
        detect_ctx->multinet->print_active_speech_commands(detect_ctx->model_data);
    }

    printf("============ detect start ============\n");
    printf("说出唤醒词 \"嗨，乐鑫\" 以激活命令词识别...\n");
    printf("已绑定动作的命令词: %d 条 (其余命令词会被忽略)\n",
           cmd_handler_get_action_count());

    /* ── 6. 启动音频处理任务 ── */
    task_flag = 1;
    /* detect_Task 固定在 CPU1：唤醒词检测 + 命令词识别 */
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void*)detect_ctx, 5, NULL, 1);
    /* feed_Task 固定在 CPU0：持续读取麦克风数据 */
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);
}
