/**
 * @file main.c
 * @brief English wake word + command recognition + hardware control
 *
 * Hardware: ESP32-S3-DevKitC-1 + INMP441 microphone
 * Model:   mn7_en (English MultiNet7) + wn9_hiesp (Hi ESP wake word)
 * GPIO:    LED=2, Fan=7, Buzzer=9
 *
 * Flow: wake word "Hi ESP" → 6s English command listening → hardware action
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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
#include "cmd_handler.h"
#include "gpio_ctrl.h"

static const esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
static srmodel_list_t *models = NULL;

/* Wake word model name (saved from afe_config before it's freed) */
static char wn_name[64] = {0};
static volatile int debug_listening_active = 0;
static volatile int debug_feed_peak = 0;
static volatile int debug_feed_avg = 0;

#define COMMAND_LISTEN_TIMEOUT_MS      6000
#define DEBUG_LISTEN_LOG_INTERVAL_MS   1000
#define DEBUG_SPEECH_PEAK_THRESHOLD    400
#define DEBUG_MIN_VOICED_CHUNKS        5

static int sample_abs_i16(int16_t sample)
{
    return sample < 0 ? -(int)sample : (int)sample;
}

static const char *mn_state_to_str(esp_mn_state_t state)
{
    switch (state) {
    case ESP_MN_STATE_DETECTING:
        return "DETECTING";
    case ESP_MN_STATE_DETECTED:
        return "DETECTED";
    case ESP_MN_STATE_TIMEOUT:
        return "TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

static void print_timeout_diagnosis(int voiced_chunks, int max_chunk_peak)
{
    if (voiced_chunks == 0) {
        printf("[DBG] No speech-like audio reached MultiNet after wake word\n");
        return;
    }

    if (voiced_chunks < DEBUG_MIN_VOICED_CHUNKS ||
        max_chunk_peak < (DEBUG_SPEECH_PEAK_THRESHOLD * 2)) {
        printf("[DBG] Speech reached MultiNet, but it was too weak/short for a full command\n");
        return;
    }

    printf("[DBG] Speech reached MultiNet, but no built-in English command matched\n");
}

/* ═══════════════════════════════════════════════════════════
 *  feed_Task: Read audio from INMP441 → feed to AFE engine
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
        if (debug_listening_active) {
            int chunk_peak = 0;
            int64_t abs_sum = 0;
            for (int i = 0; i < audio_chunksize; i++) {
                int abs_sample = sample_abs_i16(i2s_buff[i * feed_channel]);
                abs_sum += abs_sample;
                if (abs_sample > chunk_peak) {
                    chunk_peak = abs_sample;
                }
            }
            debug_feed_peak = chunk_peak;
            debug_feed_avg = (int)(abs_sum / audio_chunksize);
        }
        afe_handle->feed(afe_data, i2s_buff);
    }

    free(i2s_buff);
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════
 *  detect_Task: Wake word detection + English command recognition
 *
 *  State machine:
 *    IDLE → wake word detected → LISTENING (6s) → command/timeout → IDLE
 * ═══════════════════════════════════════════════════════════ */
void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    int chunk_ms = (afe_chunksize * 1000) / 16000;

    /* ── Initialize English MultiNet model ── */
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (mn_name == NULL) {
        printf("ERROR: mn7_en model not found!\n");
        printf("Please enable CONFIG_SR_MN_EN_MULTINET7_QUANT=y in menuconfig\n");
        vTaskDelete(NULL);
        return;
    }
    printf("MultiNet model: %s\n", mn_name);
    printf("Command path: mn7_en built-in commands only\n");

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, COMMAND_LISTEN_TIMEOUT_MS);

    /* Official example path: rebuild speech command set from sdkconfig. */
    esp_mn_commands_update_from_sdkconfig(multinet, model_data);

    /* mn7_en: use built-in commands (do NOT call set_speech_commands) */
    cmd_handler_init();

    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    /* Print built-in commands for debugging */
    multinet->print_active_speech_commands(model_data);

    printf("============ detect start ============\n");
    printf("Say \"Hi ESP\" to activate command recognition...\n");
    printf("Mapped actions: %d\n", cmd_handler_get_action_count());

    int wakeup_flag = 0;
    int listen_elapsed_ms = 0;
    int listen_frames = 0;
    int voiced_chunks = 0;
    int max_chunk_peak = 0;
    int next_listen_log_ms = DEBUG_LISTEN_LOG_INTERVAL_MS;

    while (task_flag) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            printf("fetch error!\n");
            break;
        }

        /* ── Wake word detection ── */
        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("══════════════════════════════════\n");
            printf("  🎤 Wake word detected!\n");
            printf("  Model: %s (idx:%d, word:%d)\n",
                   wn_name, res->wakenet_model_index, res->wake_word_index);
            printf("══════════════════════════════════\n");
            multinet->clean(model_data);
        }

        if (res->raw_data_channels == 1 && res->wakeup_state == WAKENET_DETECTED) {
            wakeup_flag = 1;
            debug_listening_active = 1;
            listen_elapsed_ms = 0;
            listen_frames = 0;
            voiced_chunks = 0;
            max_chunk_peak = 0;
            next_listen_log_ms = DEBUG_LISTEN_LOG_INTERVAL_MS;
            printf("-----------Listening for commands...-----------\n");
            printf("[DBG] MultiNet chunk=%d samples (~%d ms), peak threshold=%d\n",
                   afe_chunksize, chunk_ms, DEBUG_SPEECH_PEAK_THRESHOLD);
        } else if (res->raw_data_channels > 1 && res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            wakeup_flag = 1;
            debug_listening_active = 1;
            listen_elapsed_ms = 0;
            listen_frames = 0;
            voiced_chunks = 0;
            max_chunk_peak = 0;
            next_listen_log_ms = DEBUG_LISTEN_LOG_INTERVAL_MS;
            printf("-----------Channel verified, listening...-----------\n");
            printf("[DBG] MultiNet chunk=%d samples (~%d ms), peak threshold=%d\n",
                   afe_chunksize, chunk_ms, DEBUG_SPEECH_PEAK_THRESHOLD);
        }

        /* ── Command recognition ── */
        if (wakeup_flag == 1) {
            int chunk_peak = 0;
            int chunk_avg = 0;
            if (res->data != NULL) {
                int64_t abs_sum = 0;
                for (int i = 0; i < afe_chunksize; i++) {
                    int abs_sample = sample_abs_i16(res->data[i]);
                    abs_sum += abs_sample;
                    if (abs_sample > chunk_peak) {
                        chunk_peak = abs_sample;
                    }
                }
                chunk_avg = (int)(abs_sum / afe_chunksize);
            }

            listen_frames++;
            listen_elapsed_ms += chunk_ms;
            if (chunk_peak >= DEBUG_SPEECH_PEAK_THRESHOLD) {
                voiced_chunks++;
            }
            if (chunk_peak > max_chunk_peak) {
                max_chunk_peak = chunk_peak;
            }
            if (listen_elapsed_ms >= next_listen_log_ms) {
                printf("[DBG] listening=%d ms, frames=%d, feed_peak=%d, feed_avg=%d, afe_peak=%d, afe_avg=%d, voiced=%d\n",
                       listen_elapsed_ms,
                       listen_frames,
                       debug_feed_peak,
                       debug_feed_avg,
                       chunk_peak,
                       chunk_avg,
                       voiced_chunks);
                next_listen_log_ms += DEBUG_LISTEN_LOG_INTERVAL_MS;
            }

            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;
            }

            printf("[DBG] MultiNet state=%s after %d ms (frames=%d, peak=%d, voiced=%d)\n",
                   mn_state_to_str(mn_state),
                   listen_elapsed_ms,
                   listen_frames,
                   max_chunk_peak,
                   voiced_chunks);

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                if (!mn_result || mn_result->num <= 0) {
                    printf("⚠️  DETECTED but no command candidates returned\n");
                    continue;
                }

                printf("┌─── Command candidates ───┐\n");
                bool handled = false;
                for (int i = 0; i < mn_result->num; i++) {
                    printf("│ TOP %d: id=%d, phrase=%d, conf=%.2f, text=%s\n",
                           i + 1,
                           mn_result->command_id[i],
                           mn_result->phrase_id[i],
                           mn_result->prob[i],
                           mn_result->string);

                    if (!handled) {
                        handled = cmd_handler_execute(
                            mn_result->command_id[i],
                            mn_result->string,
                            mn_result->prob[i]);
                    }
                }
                printf("└──────────────────────────┘\n");

                if (handled) {
                    multinet->clean(model_data);
                    wakeup_flag = 0;
                    debug_listening_active = 0;
                    printf("-----------Command handled, waiting for wake word...-----------\n");
                } else {
                    printf("-----------No mapped command, keep listening...-----------\n");
                }
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("Timeout (%ds), input: %s\n",
                       COMMAND_LISTEN_TIMEOUT_MS / 1000,
                       (mn_result && mn_result->string[0] != '\0') ? mn_result->string : "<none>");
                print_timeout_diagnosis(voiced_chunks, max_chunk_peak);
                multinet->clean(model_data);
                wakeup_flag = 0;
                debug_listening_active = 0;
                printf("-----------Waiting for wake word...-----------\n");
            }
        }
    }

    if (model_data) {
        multinet->destroy(model_data);
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════
 *  app_main: Initialize hardware → models → AFE → start tasks
 * ═══════════════════════════════════════════════════════════ */
void app_main(void)
{
    /* 1. Audio hardware (16kHz, 1ch mono, 16bit) */
    ESP_ERROR_CHECK(esp_board_init(16000, 1, 16));

    /* 2. GPIO devices */
    ESP_ERROR_CHECK(gpio_ctrl_init());

    /* 3. Load speech models */
    models = esp_srmodel_init("model");
    if (models) {
        for (int i = 0; i < models->num; i++) {
            printf("Model: %s\n", models->model_name[i]);
        }
    }

    /* 4. Configure AFE */
    afe_config_t *afe_config = afe_config_init(
        esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);

    if (afe_config->wakenet_model_name) {
        strncpy(wn_name, afe_config->wakenet_model_name, sizeof(wn_name) - 1);
        printf("Wake word model: %s\n", wn_name);
    }

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    /* 5. Start tasks */
    task_flag = 1;
    xTaskCreatePinnedToCore(&feed_Task,   "feed",   8 * 1024, afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, afe_data, 5, NULL, 1);
}
