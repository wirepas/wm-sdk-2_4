/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef SAADC_VOLTAGE_PARAMS_H
#define SAADC_VOLTAGE_PARAMS_H

#include "hal_api.h"
#include "saadc_voltage_common.h"

/* The maximum number of 16-bit samples to transfer. */
#define MCU_NRF_SAADC_RESULT_MAXCNT (1)

/* Internal reference voltage on nRF52832 (600 mv). */
#define MCU_NRF_SAADC_INT_VREF_MV (600U)


static const voltage_params_t params_52832 = {
    .acquisitionTime = SAADC_CH_CONFIG_TACQ_10us,
    .gainControl     = SAADC_CH_CONFIG_GAIN_Gain1_6,
    .pselpValue      = (SAADC_CH_PSELP_PSELP_VDD << SAADC_CH_PSELP_PSELP_Pos),
    .inv_gain        = 6, /* 1/(1/6) */
};


__attribute__((__always_inline__)) static inline const voltage_params_t *
get_voltage_params(void)
{
    return &params_52832;
}
#endif  // SAADC_VOLTAGE_PARAMS_H
