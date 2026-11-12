/* Copyright 2020 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef RADIO_H_
#define RADIO_H_

#include "wms_radio_config.h"

/**
 * @brief   Perform application level initialization of radio
 *
 * @note    This function is only called from low level board initialization
 *          functions. There is no need to call this explicitly if
 *          [board](@ref board_folder) has been defined correctly.
 *
 */
extern void Radio_init() __attribute__((weak));

/**
 * @bried   Get pointer to custom power table
 *
 * @returns pointer to custom power table, NULL if not set
 */
const app_lib_radio_cfg_power_t * Radio_get_custom_power_table(void);

#endif /* RADIO_H_ */
