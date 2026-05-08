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
 *  混合命令词方案：313 内置 + 自定义命令
 *
 *  【设计思路】
 *  MultiNet7 在命令词较少(<50条)时置信度严重下降（模型特性），
 *  因此我们：
 *    1. 重新注册全部 313 条内置命令词（维持高置信度）
 *    2. 追加自定义命令词（如 "da kai deng", "bo fang yin yue"）
 *    3. 总计 330+ 条，确保 FST 搜索图有足够覆盖
 *
 *  【工作原理】
 *  用户说 "打开灯" → MultiNet 识别 string="da kai deng"
 *  → execute_command() 用 strcmp 在 action_map 中查找 → 执行动作
 *
 *  【添加新命令】
 *  1. 在 custom_commands[] 中添加拼音
 *  2. 在 action_map[] 中添加 {拼音, 标签, 动作ID}
 *  3. 在 execute_command() 的 switch 中添加处理
 * ═══════════════════════════════════════════════════════════
 */

/* 动作 ID 枚举 */
enum {
    ACT_LIGHT_ON = 0,
    ACT_LIGHT_OFF,
    ACT_AC_ON,
    ACT_AC_OFF,
    ACT_TEMP_UP,
    ACT_TEMP_DOWN,
    ACT_FAN_ON,
    ACT_FAN_OFF,
    ACT_FAN_UP,
    ACT_FAN_DOWN,
    ACT_MUSIC_PLAY,
    ACT_MUSIC_PAUSE,
    ACT_MUSIC_NEXT,
    ACT_MUSIC_PREV,
    ACT_VOL_UP,
    ACT_VOL_DOWN,
    ACT_CURTAIN_OPEN,
    ACT_CURTAIN_CLOSE,
};

/* ═══════════════════════════════════════════════════════════
 *  模型内置 313 条命令词（从 mn7_cn 模型中提取）
 *  这些命令词必须全部重新注册以维持 FST 搜索图的置信度。
 *  ID 使用 1~313，与模型原始编号一致。
 * ═══════════════════════════════════════════════════════════ */
static const char *builtin_commands[] = {
    "ba xiao shi hou guan ji",         // 1
    "ba xiao shi hou kai ji",          // 2
    "bi kai wo chui",                  // 3
    "chao qiang feng su",              // 4
    "chao zhe wo chui",                // 5
    "chou shi mo shi",                 // 6
    "chu shi mo shi",                  // 7
    "da kai bai feng",                 // 8
    "da kai bing xiang",               // 9
    "da kai chou shi mo shi",          // 10
    "da kai chu jun",                  // 11
    "da kai chu shi mo shi",           // 12
    "da kai dian fu re",               // 13
    "da kai ding chu feng",            // 14
    "da kai ding xiang chu wu",        // 15
    "da kai er tong fang leng feng",   // 16
    "da kai fang zhi chui",            // 17
    "da kai feng ji",                  // 18
    "da kai fu re",                    // 19
    "da kai gan zao",                  // 20
    "da kai huan ji tiao wen",         // 21
    "da kai jia shi",                  // 22
    "da kai jing hua",                 // 23
    "da kai jun yun feng",             // 24
    "da kai kong tiao",                // 25
    "da kai kong tiao deng guang",     // 26
    "da kai mu ying feng",             // 27
    "da kai mu qing feng",             // 28
    "da kai pai feng mo shi",          // 29
    "da kai pai qi",                   // 30
    "da kai pai qi mo shi",            // 31
    "da kai qiang jing",               // 32
    "da kai quan wu feng gan",         // 33
    "da kai rou feng gan",             // 34
    "da kai shang wu feng gan",        // 35
    "da kai shang xia bai feng",       // 36
    "da kai shang xia sao feng",       // 37
    "da kai sen ba",                   // 38
    "da kai shu sheng",                // 39
    "da kai shu shi feng",             // 40
    "da kai shui mian",                // 41
    "da kai si ji chu shi",            // 42
    "da kai si ji chu shi mo shi",     // 43
    "da kai song feng",                // 44
    "da kai song feng mo shi",         // 45
    "da kai wu feng gan",              // 46
    "da kai xia wu feng gan",          // 47
    "da kai xin feng",                 // 48
    "da kai xiu xian mo shi",          // 49
    "da kai xuan zhuan song feng",     // 50
    "da kai yi jian chu wu",           // 51
    "da kai zhi leng",                 // 52
    "da kai zhi leng mo shi",          // 53
    "da kai zhi re",                   // 54
    "da kai zhi re mo shi",            // 55
    "da kai zhi kong wen",             // 56
    "da kai zhi neng guan jia",        // 57
    "da kai zhi neng sha jun",         // 58
    "da kai zhi neng sheng dian",      // 59
    "da kai zhi qing jie",             // 60
    "da kai zhi wen gan",              // 61
    "da kai zhuan shu wen du",         // 62
    "da kai zi dong mo shi",           // 63
    "da kai zuo you bai feng",         // 64
    "da kai zuo you sao feng",         // 65
    "di feng su",                      // 66
    "di su feng",                      // 67
    "di yi dian",                      // 68
    "dian fu re",                      // 69
    "tiao cheng zui leng",             // 70
    "tiao cheng zui nuan",             // 71
    "tiao da feng su",                 // 72
    "tiao da xin feng",                // 73
    "tiao dao er shi ba du",           // 74
    "tiao dao er shi du",              // 75
    "tiao dao er shi er du",           // 76
    "tiao dao er shi jiu du",          // 77
    "tiao dao er shi liu du",          // 78
    "tiao dao er shi qi du",           // 79
    "tiao dao er shi san du",          // 80
    "tiao dao er shi si du",           // 81
    "tiao dao er shi wu du",           // 82
    "tiao dao er shi yi du",           // 83
    "tiao dao san shi du",             // 84
    "tiao dao shi ba du",              // 85
    "tiao dao shi jiu du",             // 86
    "tiao dao shi liu du",             // 87
    "tiao dao shi qi du",              // 88
    "tiao dao zui leng",               // 89
    "tiao dao zui nuan",               // 90
    "tiao di ling dian wu du",         // 91
    "tiao di wen du",                  // 92
    "tiao di yi du",                   // 93
    "tiao gao er shi du",              // 94
    "tiao gao ling dian wu du",        // 95
    "tiao gao wen du",                 // 96
    "tiao gao wu du",                  // 97
    "tiao gao yi du",                  // 98
    "tiao leng yi dian",               // 99
    "tiao nuan",                       // 100
    "tiao nuan yi dian",               // 101
    "tiao xiao feng su",               // 102
    "tiao xiao xin feng",              // 103
    "ding chu feng",                   // 104
    "ding shi wu rao",                 // 105
    "ding xiang chu wu",               // 106
    "er tong fang leng feng",          // 107
    "er dang xin feng",                // 108
    "feng da yi dian",                 // 109
    "feng su da dian",                 // 110
    "feng su er dang",                 // 111
    "feng su san dang",                // 112
    "feng su xiao dian",               // 113
    "feng su yi dang",                 // 114
    "feng wang shang chui",            // 115
    "feng wang xia chui",              // 116
    "feng wang you chui",              // 117
    "feng wang zhong jian chui",       // 118
    "feng wang zuo chui",              // 119
    "feng xiang shang chui",           // 120
    "feng xiang xia chui",             // 121
    "feng xiang you chui",             // 122
    "feng xiang zhong jian chui",      // 123
    "feng xiang zuo chui",             // 124
    "feng xiao yi dian",               // 125
    "gao feng su",                     // 126
    "gao su feng",                     // 127
    "guan bi bai feng",                // 128
    "fang zhi chui",                   // 129
    "guan bi dian fu re",              // 130
    "guan bi ding chu feng",           // 131
    "guan bi ding xiang chu wu",       // 132
    "guan bi er tong fang leng feng",  // 133
    "guan bi fang zhi chui",           // 134
    "guan bi feng ji",                 // 135
    "guan bi fu re",                   // 136
    "guan bi huan qi",                 // 137
    "guan bi huan qi mo shi",          // 138
    "guan bi jia shi",                 // 139
    "guan bi jie neng",                // 140
    "guan bi jun yun feng",            // 141
    "guan bi kong tiao",               // 142
    "guan bi kong tiao deng guang",    // 143
    "guan bi mu ying feng",            // 144
    "guan bi mu qing feng",            // 145
    "guan bi qiang jing",              // 146
    "guan bi quan wu feng gan",        // 147
    "guan bi rou feng gan",            // 148
    "guan bi sen ba",                  // 149
    "guan bi shang wu feng gan",       // 150
    "guan bi shang xia bai feng",      // 151
    "guan bi shang xia sao feng",      // 152
    "guan bi shu sheng",               // 153
    "guan bi shui mian",               // 154
    "guan bi si ji chu shi",           // 155
    "guan bi si ji chu shi mo shi",    // 156
    "guan bi wu feng gan",             // 157
    "guan bi xia wu feng gan",         // 158
    "guan bi xin feng",                // 159
    "guan bi xuan zhuan song feng",    // 160
    "guan bi yi jian chu wu",          // 161
    "guan bi zhi kong wen",            // 162
    "guan bi zhi qing jie",            // 163
    "guan bi zhong wen bao wen",       // 164
    "guan bi zhuan xiang wen du er",   // 165
    "guan bi zhuan xiang wen du san",  // 166
    "guan bi zhuan xiang wen du yi",   // 167
    "guan bi zuo you bai feng",        // 168
    "guan bi zuo you sao feng",        // 169
    "guan bi zhuan shu wen du",        // 170
    "guan diao bai feng",              // 171
    "guan diao dian fu re",            // 172
    "guan diao ding chu feng",         // 173
    "guan diao ding xiang chu wu",     // 174
    "guan diao er tong fang leng feng",// 175
    "guan diao fang zhi chui",         // 176
    "guan diao gan zao",               // 177
    "guan diao huan qi",               // 178
    "guan diao huan qi mo shi",        // 179
    "guan diao jia shi",               // 180
    "guan diao jing hua",              // 181
    "guan diao jun yun feng",          // 182
    "guan diao kong tiao",             // 183
    "guan diao kong tiao deng guang",  // 184
    "guan diao qiang jing",            // 185
    "guan diao quan wu feng gan",      // 186
    "guan diao rou feng gan",          // 187
    "guan diao shang wu feng gan",     // 188
    "guan diao shang xia bai feng",    // 189
    "guan diao shu sheng",             // 190
    "guan diao shui mian",             // 191
    "guan diao si ji chu shi",         // 192
    "guan diao si ji chu shi mo shi",  // 193
    "guan diao wu feng gan",           // 194
    "guan diao xia wu feng gan",       // 195
    "guan diao xin feng",              // 196
    "guan diao xuan zhuan song feng",  // 197
    "guan diao yi jian chu wu",        // 198
    "guan diao zhi kong wen",          // 199
    "guan diao zhi qing jie",          // 200
    "guan diao zuo you bai feng",      // 201
    "guan kong tiao",                  // 202
    "guan shang xia bai feng",         // 203
    "guan shang xia feng",             // 204
    "guan xin feng",                   // 205
    "guan zuo you bai feng",           // 206
    "guan zuo you feng",               // 207
    "guan bi zhi neng sheng dian",     // 208
    "jian xiao feng su",               // 209
    "jian xiao pu tong xin feng",      // 210
    "jian xiao xin feng",              // 211
    "jiang di wen du",                 // 212
    "jing yin feng",                   // 213
    "jing yin feng su",                // 214
    "jun yun feng",                    // 215
    "kai kong tiao",                   // 216
    "kai qi bai feng",                 // 217
    "kai qi chu shi",                  // 218
    "kai qi chu shi mo shi",           // 219
    "kai qi kong tiao",                // 220
    "kai qi pai feng mo shi",          // 221
    "kai qi shang xia sao feng",       // 222
    "kai qi tong feng",                // 223
    "kai qi zhi leng",                 // 224
    "kai qi zhi leng mo shi",          // 225
    "kai qi zhi re",                   // 226
    "kai qi zhi re mo shi",            // 227
    "kai qi zuo you sao feng",         // 228
    "kai shi chu wei",                 // 229
    "kong tiao da kai",                // 230
    "kong tiao guan bi",               // 231
    "kong tiao guan diao",             // 232
    "kong tiao guan ji",               // 233
    "kong tiao kai ji",                // 234
    "kong tiao kai shi pei wang",      // 235
    "kong tiao lian wang",             // 236
    "kong tiao chong xin pei wang",    // 237
    "kai xin feng",                    // 238
    "leng yi dian",                    // 239
    "nuan yi dian",                    // 240
    "pu tong xin feng",                // 241
    "qi dong kong tiao",               // 242
    "qiang jing xin feng",             // 243
    "qiang li feng",                   // 244
    "qiang li feng su",                // 245
    "qu shi mo shi",                   // 246
    "quan wu feng gan",                // 247
    "rou feng gan",                    // 248
    "san dang xin feng",               // 249
    "shang wu feng gan",               // 250
    "shang xia bai feng",              // 251
    "shang xia feng",                  // 252
    "sheng gao wen du",                // 253
    "sheng dao zui gao",               // 254
    "sheng dao zui gao wei zhi",       // 255
    "si dang xin feng",                // 256
    "si ji chu shi",                   // 257
    "si ji chu shi mo shi",            // 258
    "song feng mo shi",                // 259
    "tai leng le",                     // 260
    "tai re le",                       // 261
    "ting zhi shang xia bai feng",     // 262
    "ting zhi shang xia sao feng",     // 263
    "ting zhi bai feng",               // 264
    "ting zhi zhuan shu wen du",       // 265
    "ting zhi zuo you bai feng",       // 266
    "ting zhi zuo you sao feng",       // 267
    "xia wu feng gan",                 // 268
    "xin feng da dian",                // 269
    "xin feng er dang",                // 270
    "xin feng qiang jing",             // 271
    "xin feng qiang jing dang",        // 272
    "xin feng san dang",               // 273
    "xin feng si dang",                // 274
    "xin feng xiao dian",              // 275
    "xin feng yi dang",                // 276
    "xin feng zui da",                 // 277
    "xin feng zui da dang",            // 278
    "xin feng zui xiao",               // 279
    "xin feng zui xiao dang",          // 280
    "xuan zhuan song feng",            // 281
    "yi dang xin feng",                // 282
    "you dian leng",                   // 283
    "you dian re",                     // 284
    "zai gao yi dian",                 // 285
    "zeng da feng su",                 // 286
    "zeng da xin feng",                // 287
    "zhi leng mo shi",                 // 288
    "zhi re mo shi",                   // 289
    "zhi kong wen",                    // 290
    "zhi neng wu rao",                 // 291
    "zhi qing jie",                    // 292
    "zhong deng feng",                 // 293
    "zhong feng su",                   // 294
    "zhong su feng",                   // 295
    "zi dong",                         // 296
    "zi dong feng",                    // 297
    "zi dong feng su",                 // 298
    "zi dong mo shi",                  // 299
    "zui da feng",                     // 300
    "zui da feng su",                  // 301
    "zui da xin feng",                 // 302
    "zui xiao feng",                   // 303
    "zui xiao feng su",                // 304
    "zui xiao xin feng",               // 305
    "zuo you bai feng",                // 306
    "zuo you feng",                    // 307
    "bang wo guan deng",               // 308
    "bang wo kai deng",                // 309
    "da kai dian deng",                // 310
    "guan bi dian deng",               // 311
    "xiao le xiao le",                 // 312
    "xiao xin xiao xin",              // 313
};
static const int BUILTIN_CMD_COUNT = sizeof(builtin_commands) / sizeof(builtin_commands[0]);

/* ═══════════════════════════════════════════════════════════
 *  自定义命令词（追加到 313 条之后）
 *  这些是你自己想要的命令词，不在模型内置列表中。
 *  ID 从 400 开始，避免与内置命令冲突。
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    int id;
    const char *pinyin;
    const char *label;
} custom_cmd_t;

static const custom_cmd_t custom_commands[] = {
    /* ── 灯光（简短说法）── */
    { 400, "da kai deng",         "打开灯"   },
    { 400, "kai deng",            "开灯"     },
    { 401, "guan bi deng",        "关闭灯"   },
    { 401, "guan deng",           "关灯"     },

    /* ── 风扇 ── */
    { 402, "da kai feng shan",    "打开风扇" },
    { 403, "guan bi feng shan",   "关闭风扇" },

    /* ── 音乐 ── */
    { 404, "bo fang yin yue",     "播放音乐" },
    { 405, "zan ting yin yue",    "暂停音乐" },
    { 405, "zan ting",            "暂停"     },
    { 406, "xia yi shou",         "下一首"   },
    { 407, "shang yi shou",       "上一首"   },

    /* ── 音量 ── */
    { 408, "zeng da yin liang",   "增大音量" },
    { 408, "da sheng yi dian",    "大声一点" },
    { 409, "jian xiao yin liang", "减小音量" },
    { 409, "xiao sheng yi dian",  "小声一点" },

    /* ── 窗帘 ── */
    { 410, "da kai chuang lian",  "打开窗帘" },
    { 411, "guan bi chuang lian", "关闭窗帘" },

    /* ── 温度（同义词映射到内置命令的ID）── */
    { 412, "sheng gao yi du",     "升高一度" },
    { 413, "jiang di yi du",      "降低一度" },
};
static const int CUSTOM_CMD_COUNT = sizeof(custom_commands) / sizeof(custom_commands[0]);

/* ═══════════════════════════════════════════════════════════
 *  动作映射表（拼音 → 执行动作）
 *  同时覆盖内置命令和自定义命令的拼音。
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    const char *pinyin;
    const char *label;
    int action;
} action_entry_t;

static const action_entry_t action_map[] = {
    /* ── 灯光（内置 + 自定义都能触发）── */
    { "da kai dian deng",       "打开电灯",   ACT_LIGHT_ON   },
    { "da kai deng",            "打开灯",     ACT_LIGHT_ON   },
    { "kai deng",               "开灯",       ACT_LIGHT_ON   },
    { "bang wo kai deng",       "帮我开灯",   ACT_LIGHT_ON   },
    { "guan bi dian deng",      "关闭电灯",   ACT_LIGHT_OFF  },
    { "guan bi deng",           "关闭灯",     ACT_LIGHT_OFF  },
    { "guan deng",              "关灯",       ACT_LIGHT_OFF  },
    { "bang wo guan deng",      "帮我关灯",   ACT_LIGHT_OFF  },

    /* ── 空调 ── */
    { "da kai kong tiao",       "打开空调",   ACT_AC_ON      },
    { "kai kong tiao",          "开空调",     ACT_AC_ON      },
    { "kai qi kong tiao",       "开启空调",   ACT_AC_ON      },
    { "kong tiao kai ji",       "空调开机",   ACT_AC_ON      },
    { "kong tiao da kai",       "空调打开",   ACT_AC_ON      },
    { "qi dong kong tiao",      "启动空调",   ACT_AC_ON      },
    { "guan bi kong tiao",      "关闭空调",   ACT_AC_OFF     },
    { "guan kong tiao",         "关空调",     ACT_AC_OFF     },
    { "guan diao kong tiao",    "关掉空调",   ACT_AC_OFF     },
    { "kong tiao guan bi",      "空调关闭",   ACT_AC_OFF     },
    { "kong tiao guan diao",    "空调关掉",   ACT_AC_OFF     },
    { "kong tiao guan ji",      "空调关机",   ACT_AC_OFF     },

    /* ── 温度 ── */
    { "sheng gao yi du",        "升高一度",   ACT_TEMP_UP    },
    { "sheng gao wen du",       "升高温度",   ACT_TEMP_UP    },
    { "tiao gao yi du",         "调高一度",   ACT_TEMP_UP    },
    { "tiao gao wen du",        "调高温度",   ACT_TEMP_UP    },
    { "nuan yi dian",           "暖一点",     ACT_TEMP_UP    },
    { "tai leng le",            "太冷了",     ACT_TEMP_UP    },
    { "jiang di yi du",         "降低一度",   ACT_TEMP_DOWN  },
    { "jiang di wen du",        "降低温度",   ACT_TEMP_DOWN  },
    { "tiao di yi du",          "调低一度",   ACT_TEMP_DOWN  },
    { "tiao di wen du",         "调低温度",   ACT_TEMP_DOWN  },
    { "leng yi dian",           "冷一点",     ACT_TEMP_DOWN  },
    { "tai re le",              "太热了",     ACT_TEMP_DOWN  },

    /* ── 风扇/风速 ── */
    { "da kai feng shan",       "打开风扇",   ACT_FAN_ON     },
    { "da kai feng ji",         "打开风机",   ACT_FAN_ON     },
    { "guan bi feng shan",      "关闭风扇",   ACT_FAN_OFF    },
    { "guan bi feng ji",        "关闭风机",   ACT_FAN_OFF    },
    { "zeng da feng su",        "增大风速",   ACT_FAN_UP     },
    { "feng da yi dian",        "风大一点",   ACT_FAN_UP     },
    { "jian xiao feng su",      "减小风速",   ACT_FAN_DOWN   },
    { "feng xiao yi dian",      "风小一点",   ACT_FAN_DOWN   },

    /* ── 音乐 ── */
    { "bo fang yin yue",        "播放音乐",   ACT_MUSIC_PLAY  },
    { "zan ting yin yue",       "暂停音乐",   ACT_MUSIC_PAUSE },
    { "zan ting",               "暂停",       ACT_MUSIC_PAUSE },
    { "xia yi shou",            "下一首",     ACT_MUSIC_NEXT  },
    { "shang yi shou",          "上一首",     ACT_MUSIC_PREV  },

    /* ── 音量 ── */
    { "zeng da yin liang",      "增大音量",   ACT_VOL_UP      },
    { "da sheng yi dian",       "大声一点",   ACT_VOL_UP      },
    { "jian xiao yin liang",    "减小音量",   ACT_VOL_DOWN    },
    { "xiao sheng yi dian",     "小声一点",   ACT_VOL_DOWN    },

    /* ── 窗帘 ── */
    { "da kai chuang lian",     "打开窗帘",   ACT_CURTAIN_OPEN  },
    { "guan bi chuang lian",    "关闭窗帘",   ACT_CURTAIN_CLOSE },
};

static const int ACTION_MAP_SIZE = sizeof(action_map) / sizeof(action_map[0]);

/* ═══════════════════════════════════════════════════════════
 *  register_all_commands: 注册 313 内置 + 自定义命令词
 *
 *  流程：
 *    1. alloc() 初始化空链表
 *    2. 逐条 add() 313 条内置命令（维持 FST 覆盖率 → 高置信度）
 *    3. 逐条 add() 自定义命令（你想要的新命令）
 *    4. update() 重建 FST 搜索图
 * ═══════════════════════════════════════════════════════════ */
static void register_all_commands(const esp_mn_iface_t *multinet, model_iface_data_t *model_data)
{
    esp_mn_commands_alloc(multinet, model_data);

    /* 注册全部 313 条内置命令词 (ID 1~313) */
    int ok_count = 0, fail_count = 0;
    for (int i = 0; i < BUILTIN_CMD_COUNT; i++) {
        if (esp_mn_commands_add(i + 1, builtin_commands[i]) == ESP_OK) {
            ok_count++;
        } else {
            fail_count++;
        }
    }
    printf("┌─── 注册内置命令词: %d 成功, %d 失败 ───┐\n", ok_count, fail_count);

    /* 追加自定义命令词 (ID 400+) */
    ok_count = 0; fail_count = 0;
    for (int i = 0; i < CUSTOM_CMD_COUNT; i++) {
        if (esp_mn_commands_add(custom_commands[i].id, custom_commands[i].pinyin) == ESP_OK) {
            ok_count++;
        } else {
            printf("│ ⚠️ 自定义命令注册失败: %s (%s)\n",
                   custom_commands[i].pinyin, custom_commands[i].label);
            fail_count++;
        }
    }
    printf("│ 注册自定义命令词: %d 成功, %d 失败\n", ok_count, fail_count);
    printf("└─── 总计: %d 条命令词 ───┘\n", BUILTIN_CMD_COUNT + ok_count);

    /* 重建 FST 搜索图 */
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err != NULL) {
        printf("⚠️ %d 条命令词无法解析:\n", err->num);
        for (int i = 0; i < err->num; i++) {
            printf("   - %s\n", err->phrases[i]->string);
        }
    }
}

/*
 * ═══════════════════════════════════════════════════════════
 *  execute_command: 根据识别到的拼音文本执行动作
 *
 *  用 strcmp 逐条匹配 action_map 表，命中则执行对应动作。
 *  未在表中的命令词不做任何处理（静默忽略）。
 * ═══════════════════════════════════════════════════════════
 */
static void execute_command(const char *pinyin, float confidence)
{
    /* 跳过模型返回字符串中的前导空格 */
    while (*pinyin == ' ') pinyin++;

    /* 在动作表中查找匹配的拼音 */
    for (int i = 0; i < ACTION_MAP_SIZE; i++) {
        if (strcmp(pinyin, action_map[i].pinyin) == 0) {
            printf("✅ [%s] (置信度=%.2f)\n", action_map[i].label, confidence);

            switch (action_map[i].action) {
            case ACT_LIGHT_ON:      printf("💡 执行: 打开灯\n"); break;
            case ACT_LIGHT_OFF:     printf("💡 执行: 关闭灯\n"); break;
            case ACT_AC_ON:         printf("❄️  执行: 打开空调\n"); break;
            case ACT_AC_OFF:        printf("❄️  执行: 关闭空调\n"); break;
            case ACT_TEMP_UP:       printf("🌡️ 执行: 温度+1°C\n"); break;
            case ACT_TEMP_DOWN:     printf("🌡️ 执行: 温度-1°C\n"); break;
            case ACT_FAN_ON:        printf("🌀 执行: 打开风扇\n"); break;
            case ACT_FAN_OFF:       printf("🌀 执行: 关闭风扇\n"); break;
            case ACT_FAN_UP:        printf("🌀 执行: 风速+1\n"); break;
            case ACT_FAN_DOWN:      printf("🌀 执行: 风速-1\n"); break;
            case ACT_MUSIC_PLAY:    printf("🎵 执行: 播放音乐\n"); break;
            case ACT_MUSIC_PAUSE:   printf("🎵 执行: 暂停\n"); break;
            case ACT_MUSIC_NEXT:    printf("🎵 执行: 下一首\n"); break;
            case ACT_MUSIC_PREV:    printf("🎵 执行: 上一首\n"); break;
            case ACT_VOL_UP:        printf("🔊 执行: 音量+\n"); break;
            case ACT_VOL_DOWN:      printf("🔉 执行: 音量-\n"); break;
            case ACT_CURTAIN_OPEN:  printf("🪟 执行: 打开窗帘\n"); break;
            case ACT_CURTAIN_CLOSE: printf("🪟 执行: 关闭窗帘\n"); break;
            }
            return;
        }
    }
    /* 识别到的命令词不在动作表中，静默忽略 */
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
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);

    /*
     * 注册全部命令词：313 内置 + 自定义追加。
     * MultiNet7 需要大量命令词维持 FST 置信度，
     * 所以我们重新注册全部 313 条内置命令并追加自定义命令。
     */
    register_all_commands(multinet, model_data);

    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    multinet->print_active_speech_commands(model_data);
    printf("============ detect start ============\n");
    printf("说出唤醒词 \"嗨，乐鑫\" 以激活命令词识别...\n");
    printf("已绑定动作的命令词: %d 条 (其余命令词会被忽略)\n", ACTION_MAP_SIZE);

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
            multinet->clean(model_data);
        }

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
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("┌─── 命令词识别 ───┐\n");
                printf("│ 词=%s, 置信度=%.2f\n",
                       mn_result->string, mn_result->prob[0]);
                printf("└──────────────────┘\n");

                /* 通过拼音文本匹配动作表，执行对应动作 */
                execute_command(mn_result->string, mn_result->prob[0]);

                printf("-----------继续聆听...-----------\n");
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT) {
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

    models = esp_srmodel_init("model");
    if (models) {
        for (int i = 0; i < models->num; i++) {
            if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL)
                printf("唤醒词模型: %s\n", models->model_name[i]);
            if (strstr(models->model_name[i], ESP_MN_PREFIX) != NULL)
                printf("命令词模型: %s\n", models->model_name[i]);
        }
    }

    afe_config_t *afe_config = afe_config_init(esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);

    if (afe_config->wakenet_model_name)
        printf("AFE 唤醒词模型: %s\n", afe_config->wakenet_model_name);
    if (afe_config->wakenet_model_name_2)
        printf("AFE 唤醒词模型2: %s\n", afe_config->wakenet_model_name_2);

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void*)afe_data, 5, NULL, 1);
}
