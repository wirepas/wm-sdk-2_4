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

/* Internal reference voltage on nRF52833 and nRF52840 (600 mv). */
#define MCU_NRF_SAADC_INT_VREF_MV (600U)


/* (VDD==VBAT) */
static const voltage_params_t vdd = {
    .acquisitionTime = SAADC_CH_CONFIG_TACQ_10us,
    .gainControl     = SAADC_CH_CONFIG_GAIN_Gain1_6,
    .pselpValue      = (SAADC_CH_PSELP_PSELP_VDD << SAADC_CH_PSELP_PSELP_Pos),
    .inv_gain        = 6, /* 1/(1/6) */
};

/* (VDDH==VBAT) */
static const voltage_params_t vddh = {
    .acquisitionTime = SAADC_CH_CONFIG_TACQ_15us,
    .gainControl     = SAADC_CH_CONFIG_GAIN_Gain1_2,
    .pselpValue = (SAADC_CH_PSELP_PSELP_VDDHDIV5 << SAADC_CH_PSELP_PSELP_Pos),
    .inv_gain   = 10, /* 1/((1/2)*(1/5)) */
};


__attribute__((__always_inline__)) static inline const voltage_params_t *
get_voltage_params(void)
{
    if ((NRF_POWER->MAINREGSTATUS & POWER_MAINREGSTATUS_MAINREGSTATUS_Msk)
        == POWER_MAINREGSTATUS_MAINREGSTATUS_High)
    {
        // MCU is using VDDH as power input (VDD is programmable IO voltage):
        return &vddh;
    }
    else
    {
        // MCU is using VDD as power input (and as IO voltage):
        return &vdd;
    }
}
#endif  // SAADC_VOLTAGE_PARAMS_H
