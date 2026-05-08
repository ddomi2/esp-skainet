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
#include "esp_process_sdkconfig.h"
#include "esp_board_init.h"
#include "model_path.h"
#include "string.h"

static const esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
static srmodel_list_t *models = NULL;

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

    /* 从 sdkconfig 加载预定义的命令词列表（menuconfig 中 Speech Commands 配置） */
    esp_mn_commands_update_from_sdkconfig(multinet, model_data);

    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    /* 打印当前激活的命令词 */
    multinet->print_active_speech_commands(model_data);
    printf("------------detect start------------\n");
    printf("说出唤醒词以激活命令词识别...\n");

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

                /*
                 * TODO: 在这里根据 command_id 执行对应动作
                 * 例如: switch(mn_result->command_id[0]) {
                 *         case 0: 打开灯(); break;
                 *         case 1: 关灯();   break;
                 *       }
                 */

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
