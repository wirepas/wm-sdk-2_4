/* Bienesis Canopee v27 — nRF54L15 RS485 bridge + LIS2DW + MAX17261 board
 *
 * Pin numbering (nrf_gpio.h port-aware):
 *   P0.xx → SW_pin = xx         (port 0)
 *   P1.xx → SW_pin = 32 + xx    (port 1)
 *   P2.xx → SW_pin = 64 + xx    (port 2)
 *
 * nRF54L15 power-domain / peripheral routing (IMPORTANT):
 *   - P0 pins are in the LOW-POWER domain  → SERIAL30 (UARTE30 / SPIM30 / TWIM30)
 *   - P1/P2 pins are in the PERIPHERAL domain → SERIAL20-22 (UARTE20.. / TWIM20..)
 * A peripheral can only PSEL to pins in its own domain, so:
 *   - Console UART  = P1.07/P1.08 → UARTE20  (peripheral domain)
 *   - RS485   UART  = P0.00/P0.02 → UARTE30  (LP domain)
 *   - I2C           = P0.03/P0.04 → TWIM30   (LP domain, USE_I2C3)
 */

#ifndef BOARD_CANOPEE_V27_BOARD_H_
#define BOARD_CANOPEE_V27_BOARD_H_

/* ── Console UART (UARTE20 = SERIAL20, peripheral domain, P1 pins) ──────────── */
/* board/canopee_v27/usart_dma_series.h remaps NRF_UARTE0 → NRF_UARTE20 so the
 * Wirepas debug HAL drives the console on the P1 pins below. */
#define BOARD_USART_TX_PIN          39      /* P1.07 */
#define BOARD_USART_RX_PIN          40      /* P1.08 */

/* ── RS485 UART (UARTE30 = SERIAL30, LP domain, P0 pins) ───────────────────── */
/* RS485 signals sit on P0 → only the LP-domain UARTE30 can route to them. The
 * canopee-27 rs485_uart driver uses NRF_UARTE30 directly. */
#define BOARD_RS485_UART_RX_PIN      0      /* P0.00 — RO (receiver output → MCU RX) */
#define BOARD_RS485_UART_TX_PIN      2      /* P0.02 — DI (driver input   ← MCU TX)  */
#define BOARD_RS485_DE_PIN           1      /* P0.01 — DE, high = transmit           */

/* ── I2C — LIS2DW + MAX17261 fuel gauge (TWIM30 = SERIAL30, LP domain) ──────── */
#define USE_I2C3                            /* select TWIM30 (see i2c_defs.h) */
#define USE_MAX17261                        /* MAX17261 fuel gauge fitted     */
#define BOARD_I2C_SCL_PIN            3      /* P0.03 */
#define BOARD_I2C_SDA_PIN            4      /* P0.04 */

/* ── LED ───────────────────────────────────────────────────────────────────── */
#define BOARD_LED_PIN               41      /* P1.09  (P0 only has P0.00–P0.06) */

/* ── External supply / charger / fuel-gauge alert ──────────────────────────── */
#define BOARD_EXT_EN_PIN            37      /* P1.05 — OUT, high = enable ext supply */
#define BOARD_EXT_FAULT_PIN         38      /* P1.06 — IN,  ext supply fault         */
#define BOARD_CHG_EN_PIN            42      /* P1.10 — OUT, high = enable charger    */
#define BOARD_GAUGE_ALERT_PIN       36      /* P1.04 — IN,  MAX17261 ALRT (active-low) */

/* ── NFC antenna pins (NFCT peripheral) ────────────────────────────────────── */
#define BOARD_NFC1_PIN              34      /* P1.02 */
#define BOARD_NFC2_PIN              35      /* P1.03 */

/* ── GPIO pin list (index = GPIO ID) ──────────────────────────────────────── */
#define BOARD_GPIO_PIN_LIST          {BOARD_LED_PIN,          \
                                      BOARD_RS485_DE_PIN,     \
                                      BOARD_EXT_EN_PIN,       \
                                      BOARD_EXT_FAULT_PIN,    \
                                      BOARD_CHG_EN_PIN,       \
                                      BOARD_GAUGE_ALERT_PIN}

#define BOARD_GPIO_ID_LED            0
#define BOARD_GPIO_ID_RS485_DE       1
#define BOARD_GPIO_ID_EXT_EN         2
#define BOARD_GPIO_ID_EXT_FAULT      3
#define BOARD_GPIO_ID_CHG_EN         4
#define BOARD_GPIO_ID_GAUGE_ALERT    5

/* ── LED configuration ─────────────────────────────────────────────────────── */
#define BOARD_LED_ID_LIST            {BOARD_GPIO_ID_LED}
#define BOARD_LED_ACTIVE_LOW         false

#endif /* BOARD_CANOPEE_V27_BOARD_H_ */
