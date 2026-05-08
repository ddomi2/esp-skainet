/**
 * @file cmd_handler.h
 * @brief 命令词处理模块 — 注册命令词 + 动作映射 + 执行
 *
 * 本模块负责：
 *   1. 注册 313 条内置命令词 + 自定义命令词 → MultiNet FST
 *   2. 将识别到的拼音文本映射到具体动作
 *   3. 调用 gpio_ctrl 等模块执行硬件操作
 *
 * 使用方式：
 *   在 detect_Task 中调用 cmd_handler_register() 注册命令词，
 *   识别成功后调用 cmd_handler_execute() 执行动作。
 */
#pragma once

#include "esp_mn_iface.h"

/**
 * @brief 注册全部命令词（313 内置 + 自定义）
 *
 * 必须在 multinet->create() 之后调用。
 * 内部流程：alloc → add(313) → add(自定义) → update(重建FST)
 *
 * @param multinet    MultiNet 接口指针
 * @param model_data  MultiNet 模型数据
 */
void cmd_handler_register(const esp_mn_iface_t *multinet, model_iface_data_t *model_data);

/**
 * @brief 根据识别到的拼音执行对应动作
 *
 * 内部跳过前导空格，然后用 strcmp 在动作表中查找。
 * 匹配成功则调用硬件控制函数（如 gpio_ctrl_led_set）。
 * 未匹配的命令词静默忽略。
 *
 * @param pinyin      MultiNet 返回的拼音字符串 (mn_result->string)
 * @param confidence  识别置信度 (mn_result->prob[0])
 */
void cmd_handler_execute(const char *pinyin, float confidence);

/**
 * @brief 获取已绑定动作的命令词数量（用于启动时打印）
 *
 * @return 动作表中的条目数
 */
int cmd_handler_get_action_count(void);
