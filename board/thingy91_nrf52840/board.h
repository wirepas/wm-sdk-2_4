/* Copyright 2024 licensed under Apache License, Version 2.0
 *
 * Board definition for the nRF52840 companion chip on the Nordic Thingy:91
 * (PCA20035), used here as a Wirepas sink instead of its stock Connectivity
 * Bridge firmware.
 *
 * The nRF9160 gateway talks to this chip over the UART link that is already
 * physically wired chip-to-chip on the PCB (the same link the stock firmware
 * uses to bridge modem traces to USB) - no external wiring needed. Confirmed
 * via Nordic's official pin map (MCU_IF0-7) cross-referenced with both
 * chips' own uart1 pinctrl (see the nRF9160-side gateway repo,
 * boards/thingy91_nrf9160_ns.overlay):
 *
 *   nRF9160 uart1      nRF52840 uart1
 *   TX  P0.22 (IF4) <-> RX  P1.00 (IF4)
 *   RX  P0.23 (IF5) <-> TX  P0.25 (IF5)
 *   RTS P0.24 (IF6) <-> CTS P0.19 (IF6)
 *   CTS P0.25 (IF7) <-> RTS P0.22 (IF7)
 *
 * All other nRF52840 pins on the Thingy:91 are already committed to fixed
 * functions (RGB LED via PWM, buttons, sensors on I2C, NFC, USB) by the
 * product design, so this board intentionally defines no GPIO/LED/Button
 * list: the dual_mcu app and its drivers compile with safe dummy
 * implementations when these are left undefined (see mcu/common/led.c,
 * libraries/dualmcu/drivers/indication_signal.c).
 */
#ifndef BOARD_THINGY91_NRF52840_BOARD_H_
#define BOARD_THINGY91_NRF52840_BOARD_H_

// Serial port pins (flat GPIO numbering: P1.00 = 32 + 0 = 32)
#define BOARD_USART_TX_PIN              25  /* P0.25 */
#define BOARD_USART_RX_PIN              32  /* P1.00 */
#define BOARD_USART_CTS_PIN             19  /* P0.19 - for USE_USART_HW_FLOW_CONTROL */
#define BOARD_USART_RTS_PIN             22  /* P0.22 - for USE_USART_HW_FLOW_CONTROL */

// GPIO list: only the USART RX pin, reused by the dual_mcu app as the
// UART autopower wakeup pin (edge-detected before the UART peripheral is
// powered on). No LEDs/buttons/indication-IRQ pin on this board.
#define BOARD_GPIO_PIN_LIST            {32} /* P1.00 = BOARD_USART_RX_PIN */

#define BOARD_GPIO_ID_USART_WAKEUP      0  // mapped to pin P1.00 (= BOARD_USART_RX_PIN)

#endif /* BOARD_THINGY91_NRF52840_BOARD_H_ */
