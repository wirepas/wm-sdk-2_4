/* Copyright 2025 licensed under Apache License, Version 2.0
 *
 * Board definition for the nRF52840 Wirepas sink on the gw26 custom gateway
 * PCB. Unlike the nRF9160 DK's companion nRF52840 (routed through analog
 * switches - see board/nrf9160dk_nrf52840/board.h), gw26 wires this chip
 * directly to the nRF9160 (point-to-point PCB traces), so there is no
 * routing/board-controller dance needed here - see board_custom_init.c.
 *
 *   nRF9160 (gw26 Zephyr board)   nRF52840 (this board)
 *   uart1 TX  P0.09           <-> RX  P0.14
 *   uart1 RX  P0.10           <-> TX  P0.13
 */
#ifndef BOARD_GW26_NRF52840_BOARD_H_
#define BOARD_GW26_NRF52840_BOARD_H_

// Serial port pins (flat GPIO numbering, all on P0 here: P0.xx = xx)
#define BOARD_USART_TX_PIN              13  /* P0.13 */
#define BOARD_USART_RX_PIN              14  /* P0.14 */
// No CTS/RTS wired - matches the rest of this project's finding that
// hw-flow-control isn't needed/reliable on this chip-to-chip link.

// GPIO list: one status LED (P1.02, flat pin 32+2=34) plus the USART RX pin,
// reused by the dual_mcu app as the UART autopower wakeup pin (edge-detected
// before the UART peripheral is powered on).
#define BOARD_GPIO_PIN_LIST             {34, /* P1.02 */\
                                         14} /* P0.14 = BOARD_USART_RX_PIN */

#define BOARD_GPIO_ID_LED1               0  // mapped to pin P1.02
#define BOARD_GPIO_ID_USART_WAKEUP       1  // mapped to pin P0.14 (= BOARD_USART_RX_PIN)

// List of LED IDs
#define BOARD_LED_ID_LIST               {BOARD_GPIO_ID_LED1}

// Active low polarity for LEDs - assumed, matching most Nordic boards in
// this SDK (pca10059, pca10100, ...). Flip to false if gw26's LED is wired
// active-high.
#define BOARD_LED_ACTIVE_LOW            true

#endif /* BOARD_GW26_NRF52840_BOARD_H_ */
