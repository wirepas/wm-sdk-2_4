/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * Board definition for nRF54L15 DK:
 * https://docs.nordicsemi.com/bundle/ug_nrf54l15_dk/page/UG/nRF54L15_DK/intro/intro.html
 */

#ifndef BOARD_PCA10156_BOARD_H_
#define BOARD_PCA10156_BOARD_H_

/* NRF_GPIO is mapped to NRF_P1. */

/* Pins P0.00 - P0.04 are used via the NRF_P0 peripheral.
   With nrf_gpio.h, use SW_pin (logical pins, port-aware).

   These pins belong to the low-power domain. The maximum speed is 8 MHz. */

/**
NRF_P0  SW_pin  PCA10156                Notes (recommended usage)
------------------------------------------------------------------------
P0.00    0      gpio/UART0_TX
P0.01    1      gpio/UART0_RX
P0.02    2      gpio/UART0_RTS
P0.03    3      gpio/UART0_CTS          Clock pin
P0.04    4      gpio/BUTTON3            Clock pin
*/

/* Pins P1.00 - P1.14 are used via the NRF_P1 peripheral.
   With nrf_gpio.h, use SW_pin (logical pins, port-aware).

   These pins belong to the peripheral domain. The maximum speed is 8 MHz.

   Some pins are not connected to the headers on board. These are indicated
   with "n.c." in the Notes column. */

/**
NRF_P1  SW_pin  PCA10156                Notes (recommended usage)
------------------------------------------------------------------------
P1.00   32      [XTAL 32k]              (n.c.)
P1.01   33      [XTAL 32k]              (n.c.)
P1.02   34      gpio/NFC1               (n.c.)
P1.03   35      gpio/NFC2               Clock pin (n.c.)
P1.04   36      gpio/UART1_TX           Clock pin
P1.05   37      gpio/UART1_RX
P1.06   38      gpio/UART1_RTS
P1.07   39      gpio/UART1_CTS
P1.08   40      gpio/BUTTON2            Clock pin
P1.09   41      gpio/BUTTON1
P1.10   42      gpio/LED1
P1.11   43      gpio/AIN4               Clock pin (BOARD_GPIO_ID_UART_IRQ)
P1.12   44      gpio/AIN5               Clock pin
P1.13   45      gpio/BUTTON0
P1.14   46      gpio/LED3
*/

/* Pins P2.00 - P2.10 are used via the NRF_P2 peripheral.
   With nrf_gpio.h, use SW_pin (logical pins, port-aware).

   These pins belong to the MCU domain. The maximum speed is 64 MHz.

   Some pins are not connected to the headers on board. These are indicated
   with "n.c." in the Notes column. */
/**
NRF_P2  SW_pin  PCA10156                Notes (recommended usage)
------------------------------------------------------------------------
P2.00   64      gpio/QSPI_IO3           (n.c.)
P2.01   65      gpio/QSPI_CLK           Clock pin (n.c.)
P2.02   66      gpio/QSPI_IO0           (n.c.)
P2.03   67      gpio/QSPI_IO2           (n.c.)
P2.04   68      gpio/QSPI_IO1           (n.c.)
P2.05   69      gpio/QSPI_CS            (n.c.)
P2.06   70      gpio                    Clock pin
P2.07   71      gpio/LED2
P2.08   72      gpio
P2.09   73      gpio/LED0
P2.10   74      gpio
*/

/* Serial port pins for UART1 */
#define BOARD_USART_TX_PIN              36     /* P1.04 */
#define BOARD_USART_RX_PIN              37     /* P1.05 */
#define BOARD_USART_CTS_PIN             39     /* P1.07, for USE_USART_HW_FLOW_CONTROL */
#define BOARD_USART_RTS_PIN             38     /* P1.06, for USE_USART_HW_FLOW_CONTROL */
#define BOARD_USART_IRQ_PIN             43     /* P1.11, required by dualmcu_app */

/* GPIO pin list for pca10156 */
#define BOARD_GPIO_PIN_LIST            {73,    /* LED0 */ \
                                        42,    /* LED1 */ \
                                        71,    /* LED2 */ \
                                        46,    /* LED3 */ \
                                        45,    /* BUTTON0 */ \
                                        41,    /* BUTTON1 */ \
                                        40,    /* BUTTON2 */ \
                                        /* Required by dualmcu_app, \
                                         * USART wakeup pin (= BOARD_USART_RX) */ \
                                        BOARD_USART_RX_PIN, \
                                        /* Required by dualmcu_app, \
                                         * indication signal */ \
                                        BOARD_USART_IRQ_PIN}


/* User friendly name for GPIOs (IDs mapped to the BOARD_GPIO_PIN_LIST table) */
#define BOARD_GPIO_ID_LED0               0     /* P2.09 */
#define BOARD_GPIO_ID_LED1               1     /* P1.10 */
#define BOARD_GPIO_ID_LED2               2     /* P2.07 */
#define BOARD_GPIO_ID_LED3               3     /* P1.14 */

#define BOARD_GPIO_ID_BUTTON0            4     /* P1.13 */
#define BOARD_GPIO_ID_BUTTON1            5     /* P1.09 */
#define BOARD_GPIO_ID_BUTTON2            6     /* P1.08 */

#define BOARD_GPIO_ID_USART_WAKEUP       7     /* P1.05 */
#define BOARD_GPIO_ID_UART_IRQ           8     /* P1.11 */

/* List of LED IDs */
#define BOARD_LED_ID_LIST               {BOARD_GPIO_ID_LED0, \
                                         BOARD_GPIO_ID_LED1, \
                                         BOARD_GPIO_ID_LED2, \
                                         BOARD_GPIO_ID_LED3}

/* List of button IDs mapped to GPIO IDs */
#define BOARD_BUTTON_ID_LIST            {BOARD_GPIO_ID_BUTTON0, \
                                         BOARD_GPIO_ID_BUTTON1, \
                                         BOARD_GPIO_ID_BUTTON2}

/* Active low polarity for LEDs */
#define BOARD_LED_ACTIVE_LOW            false

/* Active low polarity for buttons */
#define BOARD_BUTTON_ACTIVE_LOW         true

/* Active internal pull-up for buttons */
#define BOARD_BUTTON_INTERNAL_PULL      true




#endif /* BOARD_PCA10156_BOARD_H_ */
