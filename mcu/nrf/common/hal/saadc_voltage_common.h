/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef SAADC_VOLTAGE_COMMON_H
#define SAADC_VOLTAGE_COMMON_H

#include <stdint.h>

/* Voltage measurement parameters specific to nRF devices. */
typedef struct voltage_params_t
{
    uint32_t acquisitionTime;
    uint32_t gainControl;
    uint32_t pselpValue;
    uint16_t inv_gain;
} voltage_params_t;


#endif  // SAADC_VOLTAGE_COMMON_H
