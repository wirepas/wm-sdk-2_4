/* Copyright 2021 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/*
 * voltage.c
 *
 *  Modified for NRF52840 optional VDDH input.
 *
 * \note SAADC one-shot mode requires that only one channel is configured!
 *       This is simple as long as we only need battery voltage measurement.
 *       Extending the A/D system requires centralized driver, because
 *       DMA writes the results of the all configured channels.
 */
#include <stdint.h>
#include "hal_api.h"
#include "saadc_voltage_common.h"
#include "saadc_voltage_params.h"

/* SAADC channel index. */
#define MCU_NRF_SAADC_CHANNEL_IDX (0U)

/* Maximum ADC value (10-bit resolution -> 1023). */
#define MCU_NRF_SAADC_MAX_ADC_VALUE (1023U)


void Mcu_voltageInit(void)
{
    /* Do nothing */
}


/**
 * Voltage measurement, NRF52840 implementation.
 * We measure either VDDH or VDD for VBAT (after checking which one is used).
 * When using VDDHDIV5 the acquisition time needs to be >= 10 µs. Use 15us.
 * SE mode, internal 0.6V ref, no oversampling, 10 bit
 * RESULT = [V(P) – V(N) ] * GAIN / REFERENCE[V] * (2**(RESOLUTION - mode))
 *     mode=0 for SingleEnded, mode=1 for Differential
 * \return millivolts
 */
uint16_t Mcu_voltageGet(void)
{
    const voltage_params_t * params     = get_voltage_params();
    volatile uint32_t        adc_result = 0;  // written by DMA

    // According to nRF52840 Product Specification v1.0 :
    // 6.23:
    //   An input channel is enabled and connected to an analog
    //   input pin using the registers CH[n].PSELP (n=0..7)
    // Clear All CH[n].PSELP registers
    // Will force a clean One-Shot operation mode
    for (uint8_t i = 0; i < 8; i++)
    {
        NRF_SAADC->CH[i].PSELP
            = (SAADC_CH_PSELP_PSELP_NC << SAADC_CH_PSELP_PSELP_Pos);
    }

    // SAADC configure:
    NRF_SAADC->RESULT.PTR = (uint32_t) &adc_result;  // set DMA target
    NRF_SAADC->RESULT.MAXCNT
        = MCU_NRF_SAADC_RESULT_MAXCNT;  // set DMA count = one result (16-bit)

    NRF_SAADC->RESOLUTION
        = (SAADC_RESOLUTION_VAL_10bit << SAADC_RESOLUTION_VAL_Pos);

    NRF_SAADC->OVERSAMPLE = (SAADC_OVERSAMPLE_OVERSAMPLE_Bypass
                             << SAADC_OVERSAMPLE_OVERSAMPLE_Pos);

    NRF_SAADC->CH[MCU_NRF_SAADC_CHANNEL_IDX].CONFIG
        = (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos)
          | (params->acquisitionTime << SAADC_CH_CONFIG_TACQ_Pos)
          | (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos)
          | (params->gainControl << SAADC_CH_CONFIG_GAIN_Pos)
          | (SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos)
          | (SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos);

    NRF_SAADC->CH[MCU_NRF_SAADC_CHANNEL_IDX].PSELN
        = (SAADC_CH_PSELN_PSELN_NC << SAADC_CH_PSELN_PSELN_Pos);

    NRF_SAADC->CH[MCU_NRF_SAADC_CHANNEL_IDX].PSELP = params->pselpValue;

    // SAADC enable (activates input access):
    NRF_SAADC->ENABLE
        = (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);

    // SAADC execute:
    NRF_SAADC->EVENTS_STARTED = 0;
    NRF_SAADC->TASKS_START    = 1;
    while (!NRF_SAADC->EVENTS_STARTED)
    {
        // Wait for SAADC start
    }

    NRF_SAADC->EVENTS_END   = 0;
    NRF_SAADC->TASKS_SAMPLE = 1;
    while (!NRF_SAADC->EVENTS_END)
    {
        // Wait for conversion to be done
    }

    // SAADC disable:
    NRF_SAADC->ENABLE
        = (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);

    NRF_SAADC->TASKS_STOP = 1;

    // Scaling the result:
    adc_result *= (MCU_NRF_SAADC_INT_VREF_MV * params->inv_gain);
    adc_result /= MCU_NRF_SAADC_MAX_ADC_VALUE;

    return (uint16_t) adc_result;
}
