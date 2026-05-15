/**
 * @file cmd_handler.c
 * @brief English-only command handler — mn7_en built-in commands → hardware
 *
 * 【设计说明】
 * mn7_en 模型在 create() 时内部加载 49 条内置命令的 FST。
 * 调用 set_speech_commands() 会替换 FST 导致检测失败（mn7 架构限制）。
 * 因此本模块不注册自定义命令，仅将内置 command_id 映射到硬件动作。
 *
 * 内置命令 ID 列表（49 条，ID 1~44）：
 *   1=tell me a joke, 2=sing a song, 3=play news channel,
 *   4=turn on my soundbox, 5=turn off my soundbox,
 *   6=highest volume, 7=lowest volume,
 *   8=increase the volume, 9=decrease the volume,
 *   10=turn on the TV, 11=turn off the TV,
 *   12=make me a tea, 13=make me a coffee,
 *   14=turn on the light, 15=turn off the light,
 *   16=change color to red, 17=change color to green,
 *   18=turn on all the lights, 19=turn off all the lights,
 *   20=turn on the air conditioner, 21=turn off the air conditioner,
 *   22~32=set temperature to 16~26 degrees,
 *   33=lowest fan speed, 34=medium fan speed, 35=highest fan speed,
 *   36=auto-adjust fan speed, 37=decrease fan speed, 38=increase fan speed,
 *   39=increase the temperature, 40=decrease the temperature,
 *   41=cool mode, 42=heat mode, 43=ventilation mode, 44=dehumidify mode
 */
#include "cmd_handler.h"
#include "gpio_ctrl.h"
#include <stdio.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════
 *  动作 ID 枚举
 * ═══════════════════════════════════════════════════════════ */
enum action_id {
    ACT_LIGHT_ON = 0,
    ACT_LIGHT_OFF,
    ACT_FAN_ON,
    ACT_FAN_OFF,
    ACT_BUZZER_ON,
    ACT_BUZZER_OFF,
    ACT_VOL_UP,
    ACT_VOL_DOWN,
};

/* ═══════════════════════════════════════════════════════════
 *  动作映射表 — mn7_en 内置 command_id → 硬件动作
 *
 *  command_id 来自模型 detect() 返回的 mn_result->command_id[0]
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    int         command_id;     /* mn7_en 内置命令 ID */
    const char *label;          /* 显示标签 */
    int         action;         /* 动作枚举 */
} action_entry_t;

typedef struct {
    int         command_id;
    const char *phrase;
    const char *action_hint;
} supported_command_t;

static const action_entry_t action_map[] = {
    /* ── LED 灯 (GPIO 2) ── */
    { 14, "Turn on the light",      ACT_LIGHT_ON  },
    { 15, "Turn off the light",     ACT_LIGHT_OFF },
    { 18, "Turn on all lights",     ACT_LIGHT_ON  },
    { 19, "Turn off all lights",    ACT_LIGHT_OFF },

    /* ── 风扇 (GPIO 7) — 复用 fan speed + air conditioner 命令 ── */
    { 35, "Highest fan speed",      ACT_FAN_ON    },
    { 38, "Increase fan speed",     ACT_FAN_ON    },
    { 34, "Medium fan speed",       ACT_FAN_ON    },
    { 33, "Lowest fan speed",       ACT_FAN_OFF   },
    { 37, "Decrease fan speed",     ACT_FAN_OFF   },
    { 20, "Turn on AC → Fan",       ACT_FAN_ON    },
    { 21, "Turn off AC → Fan",      ACT_FAN_OFF   },

    /* ── 蜂鸣器 (GPIO 9) — 复用 soundbox + song 命令 ── */
    { 4,  "Turn on soundbox",       ACT_BUZZER_ON  },
    { 2,  "Sing a song",            ACT_BUZZER_ON  },
    { 3,  "Play news channel",      ACT_BUZZER_ON  },
    { 5,  "Turn off soundbox",      ACT_BUZZER_OFF },

    /* ── 音量 (蜂鸣器 PWM 频率) ── */
    { 6,  "Highest volume",         ACT_VOL_UP    },
    { 8,  "Increase volume",        ACT_VOL_UP    },
    { 7,  "Lowest volume",          ACT_VOL_DOWN  },
    { 9,  "Decrease volume",        ACT_VOL_DOWN  },
};
static const int ACTION_MAP_SIZE = sizeof(action_map) / sizeof(action_map[0]);

static const supported_command_t supported_commands[] = {
    {  1, "tell me a joke",                  NULL },
    {  2, "sing a song",                     "Buzzer ON" },
    {  3, "play news channel",               "Buzzer ON" },
    {  4, "turn on my soundbox",             "Buzzer ON" },
    {  5, "turn off my soundbox",            "Buzzer OFF" },
    {  6, "highest volume",                  "Volume UP" },
    {  7, "lowest volume",                   "Volume DOWN" },
    {  8, "increase the volume",             "Volume UP" },
    {  9, "decrease the volume",             "Volume DOWN" },
    { 10, "turn on the TV",                  NULL },
    { 11, "turn off the TV",                 NULL },
    { 12, "make me a tea",                   NULL },
    { 13, "make me a coffee",                NULL },
    { 14, "turn on the light",               "LED ON" },
    { 15, "turn off the light",              "LED OFF" },
    { 16, "change color to red",             NULL },
    { 17, "change color to green",           NULL },
    { 18, "turn on all the lights",          "LED ON" },
    { 19, "turn off all the lights",         "LED OFF" },
    { 20, "turn on the air conditioner",     "Fan ON" },
    { 21, "turn off the air conditioner",    "Fan OFF" },
    { 22, "set temperature to 16 degrees",   NULL },
    { 23, "set temperature to 17 degrees",   NULL },
    { 24, "set temperature to 18 degrees",   NULL },
    { 25, "set temperature to 19 degrees",   NULL },
    { 26, "set temperature to 20 degrees",   NULL },
    { 27, "set temperature to 21 degrees",   NULL },
    { 28, "set temperature to 22 degrees",   NULL },
    { 29, "set temperature to 23 degrees",   NULL },
    { 30, "set temperature to 24 degrees",   NULL },
    { 31, "set temperature to 25 degrees",   NULL },
    { 32, "set temperature to 26 degrees",   NULL },
    { 33, "lowest fan speed",                "Fan OFF" },
    { 34, "medium fan speed",                "Fan ON" },
    { 35, "highest fan speed",               "Fan ON" },
    { 36, "auto-adjust fan speed",           NULL },
    { 37, "decrease fan speed",              "Fan OFF" },
    { 38, "increase fan speed",              "Fan ON" },
    { 39, "increase the temperature",        NULL },
    { 40, "decrease the temperature",        NULL },
    { 41, "cool mode",                       NULL },
    { 42, "heat mode",                       NULL },
    { 43, "ventilation mode",                NULL },
    { 44, "dehumidify mode",                 NULL },
};
static const int SUPPORTED_COMMAND_COUNT = sizeof(supported_commands) / sizeof(supported_commands[0]);

static void print_supported_commands(void)
{
    printf("┌─── Supported English commands (%d unique phrases) ───┐\n",
           SUPPORTED_COMMAND_COUNT);
    for (int i = 0; i < SUPPORTED_COMMAND_COUNT; i++) {
        if (supported_commands[i].action_hint != NULL) {
            printf("│ %2d. %-32s -> %s\n",
                   supported_commands[i].command_id,
                   supported_commands[i].phrase,
                   supported_commands[i].action_hint);
        } else {
            printf("│ %2d. %s\n",
                   supported_commands[i].command_id,
                   supported_commands[i].phrase);
        }
    }
    printf("└───────────────────────────────────────────────────────┘\n");
}

/* ═══════════════════════════════════════════════════════════ */
void cmd_handler_init(void)
{
    printf("┌─── [English] mn7_en built-in commands ───┐\n");
    printf("│ Model loads 49 commands internally.      \n");
    printf("│ Mapped %d commands to hardware actions:  \n", ACTION_MAP_SIZE);
    printf("│  💡 Light: \"turn on/off the light\"      \n");
    printf("│  🌀 Fan:   \"highest/lowest fan speed\"   \n");
    printf("│  🔊 Buzz:  \"turn on/off my soundbox\"    \n");
    printf("│  🔉 Vol:   \"increase/decrease volume\"   \n");
    printf("└──────────────────────────────────────────┘\n");
    print_supported_commands();
}

/* ═══════════════════════════════════════════════════════════ */
bool cmd_handler_execute(int command_id, const char *phonemes, float confidence)
{
    /* 在动作表中查找 */
    for (int i = 0; i < ACTION_MAP_SIZE; i++) {
        if (command_id == action_map[i].command_id) {
            printf("✅ [%s] (id=%d, conf=%.2f)\n",
                   action_map[i].label, command_id, confidence);

            switch (action_map[i].action) {
            case ACT_LIGHT_ON:
                printf("💡 LED ON\n");
                gpio_led_set(true);
                break;
            case ACT_LIGHT_OFF:
                printf("💡 LED OFF\n");
                gpio_led_set(false);
                break;
            case ACT_FAN_ON:
                printf("🌀 Fan ON\n");
                gpio_fan_set(true);
                break;
            case ACT_FAN_OFF:
                printf("🌀 Fan OFF\n");
                gpio_fan_set(false);
                break;
            case ACT_BUZZER_ON:
                printf("🔊 Buzzer ON\n");
                gpio_buzzer_set(true);
                break;
            case ACT_BUZZER_OFF:
                printf("🔊 Buzzer OFF\n");
                gpio_buzzer_set(false);
                break;
            case ACT_VOL_UP:
                printf("🔊 Volume UP → %d/10\n", gpio_buzzer_get_vol() + 1);
                gpio_buzzer_vol_up();
                break;
            case ACT_VOL_DOWN:
                printf("🔉 Volume DOWN → %d/10\n", gpio_buzzer_get_vol() - 1);
                gpio_buzzer_vol_down();
                break;
            }
            return true;
        }
    }
    /* 未映射的命令 — 仅打印 */
    printf("ℹ️  未映射命令 (id=%d, phonemes=%s, conf=%.2f)\n",
           command_id, phonemes, confidence);
    return false;
}

/* ═══════════════════════════════════════════════════════════ */
int cmd_handler_get_action_count(void)
{
    return ACTION_MAP_SIZE;
}
