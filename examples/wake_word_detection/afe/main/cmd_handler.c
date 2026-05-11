/**
 * @file cmd_handler.c
 * @brief 命令词处理实现 — 注册命令词 + 动作映射 + 执行硬件操作
 *
 * 本文件包含：
 *   1. builtin_commands[] — 模型内置 313 条命令词拼音
 *   2. custom_commands[] — 用户自定义命令词
 *   3. action_map[] — 拼音 → 动作映射表
 *   4. cmd_handler_register() — 注册所有命令词到 MultiNet FST
 *   5. cmd_handler_execute() — 根据拼音执行硬件动作
 *
 * 【扩展方法】
 *   添加新设备控制：
 *     1. 在 action_id 枚举中添加动作 ID
 *     2. 在 custom_commands[] 中添加拼音（如果不在内置313条中）
 *     3. 在 action_map[] 中添加映射
 *     4. 在 cmd_handler_execute() 的 switch 中调用硬件函数
 */
#include "cmd_handler.h"
#include "gpio_ctrl.h"
#include "esp_mn_speech_commands.h"
#include <stdio.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════
 *  动作 ID 枚举 — 每个动作对应一种硬件操作
 *  添加新设备时在此追加
 * ═══════════════════════════════════════════════════════════ */
enum action_id {
    ACT_LIGHT_ON = 0,     /* 开灯 */
    ACT_LIGHT_OFF,        /* 关灯 */
    ACT_AC_ON,            /* 开空调 */
    ACT_AC_OFF,           /* 关空调 */
    ACT_TEMP_UP,          /* 温度升高 */
    ACT_TEMP_DOWN,        /* 温度降低 */
    ACT_FAN_ON,           /* 开风扇 */
    ACT_FAN_OFF,          /* 关风扇 */
    ACT_FAN_UP,           /* 风速增大 */
    ACT_FAN_DOWN,         /* 风速减小 */
    ACT_MUSIC_PLAY,       /* 播放音乐 */
    ACT_MUSIC_PAUSE,      /* 暂停音乐 */
    ACT_MUSIC_NEXT,       /* 下一首 */
    ACT_MUSIC_PREV,       /* 上一首 */
    ACT_VOL_UP,           /* 音量增大 */
    ACT_VOL_DOWN,         /* 音量减小 */
    ACT_CURTAIN_OPEN,     /* 开窗帘 */
    ACT_CURTAIN_CLOSE,    /* 关窗帘 */
    ACT_CAR_FORWARD,      /* 遥控车前进 */
    ACT_CAR_BACKWARD,     /* 遥控车后退 */
    ACT_CAR_LEFT,         /* 遥控车左转 */
    ACT_CAR_RIGHT,        /* 遥控车右转 */
    ACT_CAR_STOP,         /* 遥控车停止 */
};

/* ═══════════════════════════════════════════════════════════
 *  模型内置 313 条命令词（从 mn7_cn 模型中提取）
 *
 *  【为什么需要全部注册？】
 *  MultiNet7 的 FST 搜索图需要 200+ 条命令词才能维持高置信度。
 *  如果只注册少量命令词（<50），所有识别结果的置信度会降到 0.15~0.40，
 *  导致完全不可用。这是模型内部数学机制决定的。
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
 *
 *  【说明】
 *  - 这些命令词不在模型内置列表中，是你自己想要的
 *  - ID 从 400 开始，避免与内置 1~313 冲突
 *  - 相同 ID 表示同义词（如 "da kai deng" 和 "kai deng" 都是开灯）
 *  - 拼音规则：全小写，空格分隔每个音节，无声调
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    int id;             /* 命令 ID，相同 ID 表示同义词 */
    const char *pinyin; /* 拼音（空格分隔音节） */
    const char *label;  /* 中文标签（仅用于日志显示） */
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
    { 405, "guan bi yin yue",    "关闭音乐" },
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

    /* ── 温度（同义词）── */
    { 412, "sheng gao yi du",     "升高一度" },
    { 413, "jiang di yi du",      "降低一度" },

    /* ── 遥控车 ── */
    { 414, "qian jin",            "前进"     },
    { 415, "hou tui",             "后退"     },
    { 416, "zuo zhuan",           "左转"     },
    { 417, "you zhuan",           "右转"     },
    { 418, "ting zhi",            "停止"     },
    { 418, "ting che",            "停车"     },
};
static const int CUSTOM_CMD_COUNT = sizeof(custom_commands) / sizeof(custom_commands[0]);

/* ═══════════════════════════════════════════════════════════
 *  动作映射表（拼音 → 执行动作）
 *
 *  【说明】
 *  - 同时覆盖内置命令和自定义命令的拼音
 *  - 多个拼音可以映射到同一个动作（如 "da kai dian deng" 和 "da kai deng" 都是开灯）
 *  - 不在此表中的识别结果会被静默忽略
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    const char *pinyin;  /* 拼音文本（必须和模型输出完全一致） */
    const char *label;   /* 中文标签（用于日志） */
    int action;          /* 动作 ID（对应 action_id 枚举） */
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
    { "guan bi yin yue",       "关闭音乐",   ACT_MUSIC_PAUSE },
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

    /* ── 遥控车 ── */
    { "qian jin",               "前进",       ACT_CAR_FORWARD   },
    { "hou tui",                "后退",       ACT_CAR_BACKWARD  },
    { "zuo zhuan",              "左转",       ACT_CAR_LEFT      },
    { "you zhuan",              "右转",       ACT_CAR_RIGHT     },
    { "ting zhi",               "停止",       ACT_CAR_STOP      },
    { "ting che",               "停车",       ACT_CAR_STOP      },
};
static const int ACTION_MAP_SIZE = sizeof(action_map) / sizeof(action_map[0]);

/* ═══════════════════════════════════════════════════════════
 *  cmd_handler_register: 注册 313 内置 + 自定义命令词
 *
 *  流程：
 *    1. alloc() 初始化空链表
 *    2. 逐条 add() 313 条内置命令（维持 FST 覆盖率 → 高置信度）
 *    3. 逐条 add() 自定义命令（你想要的新命令）
 *    4. update() 重建 FST 搜索图
 * ═══════════════════════════════════════════════════════════ */
void cmd_handler_register(const esp_mn_iface_t *multinet, model_iface_data_t *model_data)
{
    esp_mn_commands_alloc(multinet, model_data);

    /* ── 第一步：注册全部 313 条内置命令词 (ID 1~313) ── */
    int ok_count = 0, fail_count = 0;
    for (int i = 0; i < BUILTIN_CMD_COUNT; i++) {
        if (esp_mn_commands_add(i + 1, builtin_commands[i]) == ESP_OK) {
            ok_count++;
        } else {
            fail_count++;
        }
    }
    printf("┌─── 注册内置命令词: %d 成功, %d 失败 ───┐\n", ok_count, fail_count);

    /* ── 第二步：追加自定义命令词 (ID 400+) ── */
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

    /* ── 第三步：重建 FST 搜索图 ── */
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err != NULL) {
        printf("⚠️ %d 条命令词无法解析:\n", err->num);
        for (int i = 0; i < err->num; i++) {
            printf("   - %s\n", err->phrases[i]->string);
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 *  cmd_handler_execute: 根据识别到的拼音执行硬件动作
 *
 *  【工作流程】
 *  1. 跳过 MultiNet 返回字符串中的前导空格（模型特性）
 *  2. 逐条 strcmp 匹配 action_map 表
 *  3. 匹配成功 → 调用对应的硬件控制函数
 *  4. 未匹配 → 静默忽略（该命令词未绑定动作）
 * ═══════════════════════════════════════════════════════════ */
void cmd_handler_execute(const char *pinyin, float confidence)
{
    /*
     * 【重要】MultiNet7 返回的 string 带前导空格：" da kai deng"
     * 必须跳过，否则 strcmp 永远不匹配！
     */
    while (*pinyin == ' ') pinyin++;

    /* 在动作表中查找匹配的拼音 */
    for (int i = 0; i < ACTION_MAP_SIZE; i++) {
        if (strcmp(pinyin, action_map[i].pinyin) == 0) {
            printf("✅ [%s] (置信度=%.2f)\n", action_map[i].label, confidence);

            /* ── 根据动作 ID 执行硬件操作 ── */
            switch (action_map[i].action) {
            case ACT_LIGHT_ON:
                printf("💡 执行: 打开灯\n");
                gpio_led_set(true);   /* ← 实际控制 GPIO 开灯 */
                break;

            case ACT_LIGHT_OFF:
                printf("💡 执行: 关闭灯\n");
                gpio_led_set(false);  /* ← 实际控制 GPIO 关灯 */
                break;

            case ACT_AC_ON:
                printf("❄️  执行: 打开空调\n");
                /* TODO: 接入继电器或红外发射模块 */
                break;

            case ACT_AC_OFF:
                printf("❄️  执行: 关闭空调\n");
                break;

            case ACT_TEMP_UP:
                printf("🌡️ 执行: 温度+1°C\n");
                break;

            case ACT_TEMP_DOWN:
                printf("🌡️ 执行: 温度-1°C\n");
                break;

            case ACT_FAN_ON:
                printf("🌀 执行: 打开风扇\n");
                gpio_fan_set(true);   /* ← 实际控制 GPIO 开启风扇 */
                break;

            case ACT_FAN_OFF:
                printf("🌀 执行: 关闭风扇\n");
                gpio_fan_set(false);  /* ← 实际控制 GPIO 关闭风扇 */
                break;

            case ACT_FAN_UP:
                printf("🌀 执行: 风速+1\n");
                break;

            case ACT_FAN_DOWN:
                printf("🌀 执行: 风速-1\n");
                break;

            case ACT_MUSIC_PLAY:
                printf("🎵 执行: 播放音乐 (蜂鸣器开)\n");
                gpio_buzzer_set(true);   /* ← 打开蜂鸣器 */
                break;

            case ACT_MUSIC_PAUSE:
                printf("🎵 执行: 暂停 (蜂鸣器关)\n");
                gpio_buzzer_set(false);  /* ← 关闭蜂鸣器 */
                break;

            case ACT_MUSIC_NEXT:
                printf("🎵 执行: 下一首\n");
                break;

            case ACT_MUSIC_PREV:
                printf("🎵 执行: 上一首\n");
                break;

            case ACT_VOL_UP:
                printf("🔊 执行: 音量+ → %d/10\n", gpio_buzzer_get_vol() + 1);
                gpio_buzzer_vol_up();    /* ← 音量增大 */
                break;

            case ACT_VOL_DOWN:
                printf("🔉 执行: 音量- → %d/10\n", gpio_buzzer_get_vol() - 1);
                gpio_buzzer_vol_down();  /* ← 音量减小 */
                break;

            case ACT_CURTAIN_OPEN:
                printf("🪟 执行: 打开窗帘\n");
                break;

            case ACT_CURTAIN_CLOSE:
                printf("🪟 执行: 关闭窗帘\n");
                break;

            case ACT_CAR_FORWARD:
                printf("🚗 执行: 前进\n");
                gpio_motor_move(MOTOR_FORWARD);
                gpio_servo_center();            /* 方向盘回正 */
                break;

            case ACT_CAR_BACKWARD:
                printf("🚗 执行: 后退\n");
                gpio_motor_move(MOTOR_BACKWARD);
                gpio_servo_center();
                break;

            case ACT_CAR_LEFT:
                printf("🚗 执行: 左转\n");
                gpio_motor_move(MOTOR_LEFT);
                gpio_servo_set_angle(45);       /* 舵机左打 */
                break;

            case ACT_CAR_RIGHT:
                printf("🚗 执行: 右转\n");
                gpio_motor_move(MOTOR_RIGHT);
                gpio_servo_set_angle(135);      /* 舵机右打 */
                break;

            case ACT_CAR_STOP:
                printf("🚗 执行: 停止\n");
                gpio_motor_move(MOTOR_STOP);
                gpio_servo_center();            /* 方向盘回正 */
                break;
            }
            return;  /* 找到匹配即返回，不再继续查找 */
        }
    }
    /* 识别到的命令词不在动作表中 → 静默忽略 */
}

/**
 * @brief 获取动作表条目数
 */
int cmd_handler_get_action_count(void)
{
    return ACTION_MAP_SIZE;
}
