/* Bienesis sensorv26 — nRF54L15 RS485 master board definition
 *
 * Adjust pin numbers to match the actual PCB schematic.
 * SW_pin = logical pin number used by nrf_gpio.h (port-aware):
 *   P0.xx → SW_pin = xx        (port 0, 5 pins)
 *   P1.xx → SW_pin = 32 + xx   (port 1, 15 pins)
 *   P2.xx → SW_pin = 64 + xx   (port 2, 11 pins)
 */

#ifndef BOARD_SENSORV26_BOARD_H_
#define BOARD_SENSORV26_BOARD_H_

/* ── UART (RS485 bus) ──────────────────────────────────────────────────────── */
#define BOARD_USART_TX_PIN          36      /* P1.04 */
#define BOARD_USART_RX_PIN          37      /* P1.05 */

/* Hardware flow control is NOT used (RS485 half-duplex via DE pin below).
 * Define dummy CTS/RTS to avoid compilation errors; they are never driven. */
#define BOARD_USART_CTS_PIN         39      /* P1.07 — unused */
#define BOARD_USART_RTS_PIN         38      /* P1.06 — unused */

/* Required by the Wirepas dualmcu library for UART-based wake-up signalling.
 * Not used in rs485_bridge but must be defined for the board HAL. */
#define BOARD_USART_IRQ_PIN         43      /* P1.11 */

/* ── RS485 direction control ───────────────────────────────────────────────── */
/* DE (Driver Enable): HIGH = transmit, LOW = receive.
 * Connect to the ~RE pin of the transceiver as well if DE/~RE are separate. */
#define BOARD_RS485_DE_PIN          40      /* P1.08 — adjust to schematic */

/* ── Status LED ────────────────────────────────────────────────────────────── */
#define BOARD_GPIO_PIN_LIST         {73,                    /* LED — P2.09     */ \
                                     BOARD_RS485_DE_PIN,    /* RS485 DE        */ \
                                     BOARD_USART_RX_PIN,    /* USART wakeup    */ \
                                     BOARD_USART_IRQ_PIN}   /* UART IRQ        */

#define BOARD_GPIO_ID_LED0                  0   /* P2.09 */
#define BOARD_GPIO_ID_RS485_DE              1   /* RS485 Driver Enable */
#define BOARD_GPIO_ID_USART_WAKEUP          2   /* maps to BOARD_USART_RX_PIN */
#define BOARD_GPIO_ID_UART_IRQ              3   /* maps to BOARD_USART_IRQ_PIN */

#define BOARD_LED_ID_LIST                   {BOARD_GPIO_ID_LED0}
#define BOARD_LED_ACTIVE_LOW                false

/* No physical buttons on the master board */
#define BOARD_BUTTON_ID_LIST                {}

#endif /* BOARD_SENSORV26_BOARD_H_ */
