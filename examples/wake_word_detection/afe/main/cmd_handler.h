/**
 * @file cmd_handler.h
 * @brief English command handler — map mn7_en built-in commands to hardware
 *
 * mn7_en model loads 49 built-in commands during create().
 * This module maps relevant command IDs to LED/Fan/Buzzer actions.
 *
 * Usage:
 *   1. cmd_handler_init() after multinet->create()
 *   2. cmd_handler_execute() when command detected
 */
#pragma once

#include <stdbool.h>
#include "esp_mn_iface.h"

/**
 * @brief Initialize command handler (prints mapped commands info)
 */
void cmd_handler_init(void);

/**
 * @brief Execute hardware action based on detected command_id
 *
 * @param command_id  mn7_en built-in command ID (1~44)
 * @param phonemes    Phoneme string returned by model (for debug display)
 * @param confidence  Detection confidence
 */
bool cmd_handler_execute(int command_id, const char *phonemes, float confidence);

/**
 * @brief Get number of mapped action entries
 */
int cmd_handler_get_action_count(void);
