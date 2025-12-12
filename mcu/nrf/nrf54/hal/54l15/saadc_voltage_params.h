/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef SAADC_VOLTAGE_PARAMS_H
#define SAADC_VOLTAGE_PARAMS_H

#include "hal_api.h"
#include "saadc_voltage_common.h"

#define SAADC_CH_PSELP_PSELP_NC  SAADC_CH_PSELP_CONNECT_NC
#define SAADC_CH_PSELP_PSELP_Pos SAADC_CH_PSELP_CONNECT_Pos
#define SAADC_CH_PSELN_PSELN_NC  SAADC_CH_PSELN_CONNECT_NC
#define SAADC_CH_PSELN_PSELN_Pos SAADC_CH_PSELN_CONNECT_Pos

// in nRF54L15 SAADC there are no control for resitors
#define SAADC_CH_CONFIG_RESP_Bypass 0
#define SAADC_CH_CONFIG_RESP_Pos    0
#define SAADC_CH_CONFIG_RESN_Bypass 0
#define SAADC_CH_CONFIG_RESN_Pos    0

/* The maximum number of bytes to transfer.
 * One 16-bit sample -> two bytes. */
#define MCU_NRF_SAADC_RESULT_MAXCNT (2)

/* Internal reference voltage on nRF54L15 (900 mv). */
#define MCU_NRF_SAADC_INT_VREF_MV (900U)


static const voltage_params_t params_54l15 = {
    /* TACQ @Bits 16..24 : Acquisition time, the time the ADC uses to sample the
       input voltage. Resulting acquistion time is
                           ((TACQ+1) x 125 ns)  so  10us = 79+1*/
    .acquisitionTime = 79,
    .gainControl     = SAADC_CH_CONFIG_GAIN_Gain2_8,
    .pselpValue
    = ((SAADC_CH_PSELP_INTERNAL_Vdd << SAADC_CH_PSELP_INTERNAL_Pos)
       | (SAADC_CH_PSELP_CONNECT_Internal << SAADC_CH_PSELP_CONNECT_Pos)),
    .inv_gain = 4, /* 1/(2/8) */
};


__attribute__((__always_inline__)) static inline const voltage_params_t *
get_voltage_params(void)
{
    return &params_54l15;
}
#endif  // SAADC_VOLTAGE_PARAMS_H
