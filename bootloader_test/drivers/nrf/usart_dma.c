/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>

#include "board.h"
#include "mcu.h"
#include "usart_dma_series.h"


void Usart_init(uint32_t baudrate)
{
    /* GPIO init */
    nrf_gpio_cfg(BOARD_USART_TX_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0S1,
                 NRF_GPIO_PIN_NOSENSE);

    NRF_UARTE0->PSEL.TXD    = BOARD_USART_TX_PIN;
    NRF_UARTE0->PSEL.RXD    = BOARD_USART_RX_PIN;
    NRF_UARTE0_TASKS_STOPTX = 1;

    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Enabled;

    /* Set flow control pins */
    NRF_UARTE0->PSEL.RTS = 0xFFFFFFFF;
    NRF_UARTE0->PSEL.CTS = 0xFFFFFFFF;

    /* No HW flow control, no parity, one stop bit */
    uint32_t uarte_config
        = (UARTE_CONFIG_HWFC_Disabled << UARTE_CONFIG_HWFC_Pos)
          | (UARTE_CONFIG_PARITY_Excluded << UARTE_CONFIG_PARITY_Pos);

#if !defined(NRF52832_XXAA)
    uarte_config |= (UARTE_CONFIG_STOP_One << UARTE_CONFIG_STOP_Pos);
#endif  // !defined(NRF52832_XXAA)

    NRF_UARTE0->CONFIG = uarte_config;

    /* Serial port init */
    switch (baudrate)
    {
        case 115200:
            NRF_UARTE0->BAUDRATE
                = (uint32_t) UARTE_BAUDRATE_BAUDRATE_Baud115200;
            break;
        case 125000:
            /* UART_BAUDRATE_BAUDRATE_Baud125000 is not defined by Nordic */
            NRF_UARTE0->BAUDRATE = (uint32_t) (0x02000000UL);
            break;
        case 1000000:
            NRF_UARTE0->BAUDRATE = (uint32_t) UARTE_BAUDRATE_BAUDRATE_Baud1M;
            break;
        default:
            break;
    }

    NRF_UARTE0_EVENTS_ENDTX = 0;
}


uint32_t Usart_sendBuffer(const void * buffer, uint32_t length)
{
    uint32_t  sent       = 0;
    uint8_t * dma_buffer = (uint8_t *) buffer;
    while (sent < length)
    {
        uint32_t remaining_bytes = length - sent;
        uint32_t maxcnt          = ((remaining_bytes < NRF_UARTE0_TX_MAXCNT_MAX)
                                        ? remaining_bytes
                                        : NRF_UARTE0_TX_MAXCNT_MAX);

        NRF_UARTE0_TX_PTR        = (uint32_t) &dma_buffer[sent];
        NRF_UARTE0_TX_MAXCNT     = maxcnt;
        NRF_UARTE0_TASKS_STARTTX = 1;
        while (!NRF_UARTE0_EVENTS_ENDTX)
        {
        }

        NRF_UARTE0_EVENTS_ENDTX = 0;
        NRF_UARTE0_TASKS_STOPTX = 1;
        sent += maxcnt;
    }

    return sent;
}
