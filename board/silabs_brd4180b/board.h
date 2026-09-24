/* Copyright 2020 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * @file
 *
 * Board definition for a board composed of a
 * <a href="https://www.silabs.com/products/development-tools/wireless/proprietary/slwstk6005a-sub-ghz-bluetooth-multiband-wireless-starter-kit">Silabs starter kit</a>
 * and a <a href="https://www.silabs.com/documents/public/user-guides/ug427-brd4180b-user-guide.pdf">brd4180b radio module</a>
 */
#ifndef BOARD_SILABS_BRD4180B_BOARD_H_
#define BOARD_SILABS_BRD4180B_BOARD_H_

// Serial port
#define BOARD_USART_ID                  0
#define BOARD_USART_TX_PORT             GPIO_PORTA
#define BOARD_USART_TX_PIN              5
#define BOARD_USART_RX_PORT             GPIO_PORTA
#define BOARD_USART_RX_PIN              6

// NOTE! On the BRD4001A WSTK mainboard the console is routed through the
// board-controller VCOM bridge, which supports ONLY standard baud rates.
// The Wirepas default (125000) does not work over VCOM, so force 115200.
#define BOARD_USART_FORCE_BAUDRATE      115200

// List of GPIO pins
#define BOARD_GPIO_PIN_LIST            {{GPIO_PORTD, 2}, /* PD02 */\
                                        {GPIO_PORTD, 3}, /* PD03 */\
                                        {GPIO_PORTB, 0}, /* PB00 */\
                                        {GPIO_PORTB, 1}, /* PB01 */\
                                        {GPIO_PORTA, 6}, /* PA06. required by the dual_mcu app. usart wakeup pin (= BOARD_USART_RX) */\
                                        {GPIO_PORTD, 4}} /* PD04. usart vcom pin */

// User friendly name for GPIOs (IDs mapped to the BOARD_GPIO_PIN_LIST table)
#define BOARD_GPIO_ID_LED0              0  // mapped to pin PD02
#define BOARD_GPIO_ID_LED1              1  // mapped to pin PD03
#define BOARD_GPIO_ID_BUTTON0           2  // mapped to pin PB00
#define BOARD_GPIO_ID_BUTTON1           3  // mapped to pin PB01
#define BOARD_GPIO_ID_USART_WAKEUP      4  // mapped to pin PA06
#define BOARD_GPIO_ID_VCOM_ENABLE       5  // mapped to pin PD04

// List of LED IDs
#define BOARD_LED_ID_LIST              {BOARD_GPIO_ID_LED0, BOARD_GPIO_ID_LED1}

// Blink this LED (index into BOARD_LED_ID_LIST) on every dual-MCU indication
// poll. Board-specific debug aid: only boards that define this macro blink.
#define BOARD_LED_BLINK_ON_POLL        0  // LED0 (PD02)

// Active high polarity for LEDs
#define BOARD_LED_ACTIVE_LOW false

// List of button IDs
#define BOARD_BUTTON_ID_LIST           {BOARD_GPIO_ID_BUTTON0, BOARD_GPIO_ID_BUTTON1}

// Active low polarity for buttons
#define BOARD_BUTTON_ACTIVE_LOW         true

// Board has external pull-up for buttons
#define BOARD_BUTTON_INTERNAL_PULL      false

#endif /* BOARD_SILABS_BRD4180B_BOARD_H_ */
