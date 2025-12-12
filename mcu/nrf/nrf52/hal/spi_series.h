/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef NRF_SPI_SERIES_H
#define NRF_SPI_SERIES_H

#include <stdint.h>
#include <stdbool.h>

#include "mcu.h"
#include "hal/nrf_spim.h"

#if defined(USE_SPI0)
#define SPI_IRQn SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn
#define SPI_DEV  NRF_SPIM0
#elif defined(USE_SPI1)
#define SPI_IRQn SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn
#define SPI_DEV  NRF_SPIM1
#elif defined(USE_SPI2)
#define SPI_IRQn SPIM2_SPIS2_SPI2_IRQn
#define SPI_DEV  NRF_SPIM2
#elif defined(USE_SPI3)
#if MCU_SUB == 832
#error SPI3 is not available on nrf52832
#endif
#define SPI_IRQn SPIM3_IRQn
#define SPI_DEV  NRF_SPIM3
#else
#error You must specify either USE_SPI0, USE_SPI1, USE_SPI2 or USE_SPI3 (nrf52833 and nrf52840 only) in your board.h
#endif

/* Maximum SPI transfer size for the nRF52 series. */
#if MCU_SUB == 832
#define MAX_XFER_SIZE (UINT8_MAX)
#else
#define MAX_XFER_SIZE (UINT16_MAX)
#endif


/**
 * \brief   Set the frequency of the SPI module
 * \param   freq
 *          Frequency requested in Hz
 * \return  True if set successfully, false otherwise
 */
__attribute__((__always_inline__)) static inline bool set_frequency(
    uint32_t freq)
{
    uint32_t nrf_freq;
    switch (freq)
    {
        case 125000:
            nrf_freq = NRF_SPIM_FREQ_125K;
            break;
        case 250000:
            nrf_freq = NRF_SPIM_FREQ_250K;
            break;
        case 500000:
            nrf_freq = NRF_SPIM_FREQ_500K;
            break;
        case 1000000:
            nrf_freq = NRF_SPIM_FREQ_1M;
            break;
        case 2000000:
            nrf_freq = NRF_SPIM_FREQ_2M;
            break;
        case 4000000:
            nrf_freq = NRF_SPIM_FREQ_4M;
            break;
        case 8000000:
            nrf_freq = NRF_SPIM_FREQ_8M;
            break;
        default:
            return false;
    }

    nrf_spim_frequency_set(SPI_DEV, nrf_freq);
    return true;
}


/**
 * \brief   Get GPIO pin drive configuration based on requested SPI frequency.
 * \param   freq
 *          Frequency requested in Hz
 * \return  GPIO pin drive configuration
 */
__attribute__((__always_inline__)) static inline nrf_gpio_pin_drive_t
get_gpio_pin_drive_conf(uint32_t freq)
{
    (void) freq;
    return NRF_GPIO_PIN_S0S1;
}

#endif  // NRF_SPI_SERIES_H
