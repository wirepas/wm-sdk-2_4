/* Copyright 2024 licensed under Apache License, Version 2.0
 *
 * Board definition for the companion nRF52840 on the Nordic nRF9160 DK
 * (PCA10090), used here as a Wirepas sink instead of its stock board
 * controller / connectivity bridge firmware.
 *
 * Unlike the Thingy:91 (direct chip-to-chip PCB traces), the nRF9160 DK
 * routes its "nRF interface" pins (IF0-IF8) through analog switches that the
 * board controller firmware normally configures - see board_custom_init.c,
 * which enables the two switch banks our UART lines need. Pin mapping
 * confirmed via Nordic's own Zephyr board files for this DK (nRF52840 side of
 * boards/nordic/nrf9160dk/dts/nrf52840/nrf9160dk_uart1_on_if0_3.dtsi):
 *
 *   nRF9160 uart1      nRF52840 uart1
 *   TX  P0.18      <-> RX  P0.20
 *   RX  P0.17      <-> TX  P0.17
 *   RTS P0.21      <-> CTS P0.22
 *   CTS P0.19      <-> RTS P0.15
 *
 * These IF0-IF3 lines span both switch banks (nrf9160dk_nrf52840.dts:
 * switch-nrf-if0-2-ctrl on P0.13, switch-nrf-if3-5-ctrl on P0.24, both
 * active-high) - both must be driven high, done in board_custom_init.c
 * before anything tries to use the UART.
 */
#ifndef BOARD_NRF9160DK_NRF52840_BOARD_H_
#define BOARD_NRF9160DK_NRF52840_BOARD_H_

// Serial port pins (flat GPIO numbering, all on P0 here: P0.xx = xx)
#define BOARD_USART_TX_PIN              17  /* P0.17 */
#define BOARD_USART_RX_PIN              20  /* P0.20 */
#define BOARD_USART_CTS_PIN             22  /* P0.22 - for USE_USART_HW_FLOW_CONTROL */
#define BOARD_USART_RTS_PIN             15  /* P0.15 - for USE_USART_HW_FLOW_CONTROL */

// GPIO list: only the USART RX pin, reused by the dual_mcu app as the
// UART autopower wakeup pin (edge-detected before the UART peripheral is
// powered on). No LEDs/buttons/indication-IRQ pin on this board.
#define BOARD_GPIO_PIN_LIST            {20} /* P0.20 = BOARD_USART_RX_PIN */

#define BOARD_GPIO_ID_USART_WAKEUP      0  // mapped to pin P0.20 (= BOARD_USART_RX_PIN)

#endif /* BOARD_NRF9160DK_NRF52840_BOARD_H_ */
