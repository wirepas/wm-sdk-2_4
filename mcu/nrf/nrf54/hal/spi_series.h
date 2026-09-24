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
#define SPI_IRQn SPIM00_IRQn
#define SPI_DEV  NRF_SPIM00
#elif defined(USE_SPI1)
#define SPI_IRQn SPIM20_IRQn
#define SPI_DEV  NRF_SPIM20
#elif defined(USE_SPI2)
#define SPI_IRQn SPIM21_IRQn
#define SPI_DEV  NRF_SPIM21
#elif defined(USE_SPI3)
#define SPI_IRQn SPIM22_IRQn
#define SPI_DEV  NRF_SPIM22
#elif defined(USE_SPI4)
#define SPI_IRQn SPIM30_IRQn
#define SPI_DEV  NRF_SPIM30
#else
#error You must specify either USE_SPI0, USE_SPI1, USE_SPI2, USE_SPI3 or USE_SPI4 in your board.h
#endif

/* NRF_SPIM00 supports higher SPI frequencies. */
#if defined(USE_SPI0)
#define SPI_MIN_FREQ_HZ  (2000000UL)
#define SPI_MAX_FREQ_HZ  (32000000UL)
#else
#define SPI_MIN_FREQ_HZ  (250000UL)
#define SPI_MAX_FREQ_HZ  (8000000UL)
#endif

/* Maximum SPI transfer size for the nRF54L series. */
#define MAX_XFER_SIZE (UINT16_MAX)


/**
 * \brief   Set the frequency of the SPI module
 * \param   freq
 *          Frequency requested in Hz
 * \return  True if set successfully, false otherwise
 */
__attribute__((__always_inline__)) static inline bool set_frequency(
    uint32_t freq)
{
    /* SPI frequency is calculated by dividing peripheral clock frequency with a
       prescaler. These depend on the used peripheral instance:
       - NRF_SPIM00:
         * peripheral clock frequency: 128 MHz
         * prescaler divisor range: [4..126] (even divisors only)
         * SPI frequency range: [~1015873..32000000] Hz
       - all other instances:
         * peripheral clock frequency: 16 MHz
         * prescaler divisor range: [2..126] (even divisors only)
         * SPI frequency range: [~126984..8000000] Hz
    */
    if ((freq < SPI_MIN_FREQ_HZ) || (freq > SPI_MAX_FREQ_HZ))
    {
        return false;
    }

    uint32_t prescaler;
    switch (freq)
    {
        case 250000:
            // Intentional fall through

        case 500000:
            // Intentional fall through

        case 1000000:
            // Intentional fall through

        case 2000000:
            // Intentional fall through

        case 4000000:
            // Intentional fall through

        case 8000000:
            // Intentional fall through

        case 16000000:
            // Intentional fall through

        case 32000000:
            prescaler = NRF_SPIM_PRESCALER_CALCULATE(SPI_DEV, freq);
            break;

        default:
            return false;
    }

    nrf_spim_prescaler_set(SPI_DEV, prescaler);
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
    return ((freq == 32000000) ? NRF_GPIO_PIN_E0E1 : NRF_GPIO_PIN_S0S1);
}

#endif  // NRF_SPI_SERIES_H
