/* Copyright 2018 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>

#include "board.h"
#include "mcu.h"
#include "board_usart.h"

#if defined(_SILICON_LABS_32B_SERIES_2)
#include "em_usart.h"
#include "em_cmu.h"
#include "em_chip.h"

#include "em_device.h"


void initCMU(void)
{
  // Enable clock to GPIO and USART0
  CMU_ClockEnable(cmuClock_GPIO, true);
  CMU_ClockEnable(cmuClock_USART0, true);
}

/**************************************************************************//**
 * @brief
 *    GPIO initialization
 *****************************************************************************/
void initGPIO(void)
{
  // Configure the USART TX pin to the board controller as an output
  GPIO_PinModeSet(BOARD_USART_TX_PORT, BOARD_USART_TX_PIN, gpioModePushPull, 0);

  // Configure the USART RX pin to the board controller as an input
  GPIO_PinModeSet(BOARD_USART_RX_PORT, BOARD_USART_RX_PIN, gpioModeInput, 0);

}

/**************************************************************************//**
 * @brief
 *    USART0 initialization
 *****************************************************************************/
void initUSART0(void)
{
  // Default asynchronous initializer (115.2 Kbps, 8N1, no flow control)
  USART_InitAsync_TypeDef init = USART_INITASYNC_DEFAULT;

  // Route USART0 TX and RX to the board controller TX and RX pins
  GPIO->USARTROUTE[0].TXROUTE = (BOARD_USART_TX_PORT << _GPIO_USART_TXROUTE_PORT_SHIFT)
            | (BOARD_USART_TX_PIN << _GPIO_USART_TXROUTE_PIN_SHIFT);
  GPIO->USARTROUTE[0].RXROUTE = (BOARD_USART_RX_PORT << _GPIO_USART_RXROUTE_PORT_SHIFT)
            | (BOARD_USART_RX_PIN << _GPIO_USART_RXROUTE_PIN_SHIFT);

  // Enable RX and TX signals now that they have been routed
  GPIO->USARTROUTE[0].ROUTEEN = GPIO_USART_ROUTEEN_RXPEN | GPIO_USART_ROUTEEN_TXPEN;

  // Configure and enable USART0
  USART_InitAsync(USART0, &init);
}

void Usart_init(uint32_t baudrate)
{
    CHIP_Init();
    // Initialize GPIO and USART0
    initCMU();
    
    initGPIO();
    initUSART0();

    return;
}

uint32_t Usart_sendBuffer(const void * buffer, uint32_t length)
{
    uint32_t sent = 0;
    uint8_t * _buffer = (uint8_t *) buffer;

    while (length--)
    {
        USART_Tx(USART0, _buffer[sent]);
        sent++;
    }

    return sent;

}

#else

static uint32_t getHFPERCLK()
{
    uint32_t div;
    div = 1U + ((CMU->HFPERPRESC & _CMU_HFPERPRESC_PRESC_MASK) >>
                                                _CMU_HFPERPRESC_PRESC_SHIFT);
    return 38000000 / div;
}

static void set_baud(uint32_t baud)
{
    volatile uint32_t baud_gen;
    /* Calculate baudrate: see em_usart.c in emlib for reference */
    baud_gen = 32 * getHFPERCLK() + (4 * baud) / 2;
    baud_gen /= (4 * baud);
    baud_gen -= 32;
    baud_gen *= 8;
    baud_gen &= _USART_CLKDIV_DIV_MASK;

    /* Set oversampling bit (8) */
    BOARD_USART->CTRL  &= ~_USART_CTRL_OVS_MASK;
    BOARD_USART->CTRL  |= USART_CTRL_OVS_X4;
    BOARD_USART->CLKDIV = baud_gen;
}

void Usart_init(uint32_t baudrate)
{
    /* Wait for HFRCO to be ready */
    while ((CMU->SYNCBUSY & CMU_SYNCBUSY_HFRCOBSY) ||
           !(CMU->STATUS & _CMU_STATUS_HFRCORDY_MASK)) {}

    /* Set 1 wait state */
    MSC->READCTRL = (MSC->READCTRL & ~_MSC_READCTRL_MODE_MASK) |
                    MSC_READCTRL_MODE_WS1;

    /* Set HFRCO to 38 MHz */
    CMU->HFRCOCTRL = DEVINFO->HFRCOCAL12;

     /* Enable clocks */
    CMU->HFBUSCLKEN0 |= CMU_HFBUSCLKEN0_GPIO;

    /* Configure Uart Tx pin */
    hal_gpio_set_mode(BOARD_USART_TX_PORT,
                      BOARD_USART_TX_PIN,
                      GPIO_MODE_OUT_PP);
    hal_gpio_clear(BOARD_USART_TX_PORT, BOARD_USART_TX_PIN);

    /* Must enable clock for configuration period */
    CMU->HFPERCLKEN0 |= BOARD_USART_CMU_BIT;

    /* Set UART output pins */
    BOARD_USART->ROUTEPEN = USART_ROUTEPEN_TXPEN;

    /* Set UART route */
    BOARD_USART->ROUTELOC0 = BOARD_USART_ROUTELOC_TXLOC;

    /* Initialize UART for asynch mode with baudrate baud */
    /* Disable transceiver */
    BOARD_USART->CMD = 0;
    BOARD_USART->CTRL = 0;
    BOARD_USART->I2SCTRL = 0;
    /* Disables PRS */
    BOARD_USART->INPUT = 0;
    /* Set frame params: 8bit, nopar, 1stop */
    BOARD_USART->FRAME = USART_FRAME_DATABITS_EIGHT |
                         USART_FRAME_PARITY_NONE |
                         USART_FRAME_STOPBITS_ONE;
    set_baud(baudrate);

    /* Enable transmitter */
    BOARD_USART->CMD = USART_CMD_TXEN;
}

uint32_t Usart_sendBuffer(const void * buffer, uint32_t length)
{
    uint32_t sent = 0;
    uint8_t * _buffer = (uint8_t *) buffer;

    while (length--)
    {
        BOARD_USART->TXDATA = _buffer[sent++];
        while (!(BOARD_USART->STATUS & USART_STATUS_TXC));
    }

    return sent;
}
#endif
