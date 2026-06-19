/* Bienesis sensorv26 — nRF54L15 RS485 bridge board definition
 *
 * Pin numbering (nrf_gpio.h port-aware):
 *   P0.xx → SW_pin = xx         (port 0)
 *   P1.xx → SW_pin = 32 + xx    (port 1)
 *   P2.xx → SW_pin = 64 + xx    (port 2)
 */

#ifndef BOARD_SENSORV26_BOARD_H_
#define BOARD_SENSORV26_BOARD_H_

/* ── Console UART (UARTE30 = SERIAL30, LP domain) ─────────────────────────── */
/* P0 pins are LP-domain only → must use UARTE30. board/sensorv26/usart_dma_series.h
 * remaps NRF_UARTE0 → NRF_UARTE30 so the Wirepas debug HAL uses the right peripheral. */
#define BOARD_USART_TX_PIN           1      /* P0.01 */
#define BOARD_USART_RX_PIN           0      /* P0.00 */

/* ── RS485 UART (UARTE20 = SERIAL20, peripheral domain) ────────────────────── */
#define BOARD_RS485_UART_TX_PIN     36      /* P1.04 */
#define BOARD_RS485_UART_RX_PIN     37      /* P1.05 */
#define BOARD_RS485_DE_PIN          38      /* P1.06 — high = transmit */

/* ── I2C1 — AEM10900 PMIC + LIS2DW (TWIM21 = SERIAL21) ─────────────────────── */
#define USE_I2C1
/* #define USE_AEM10900 */   /* AEM10900 not populated — keep commented until fitted */
#define BOARD_I2C_SCL_PIN           43      /* P1.11 */
#define BOARD_I2C_SDA_PIN           44      /* P1.12 */

/* ── SPI3 — ADS1220 24-bit ADC (SPIM22 = SERIAL22) ─────────────────────────── */
#define USE_SPI3
#define BOARD_SPI_SCK_PIN           70      /* P2.06 */
#define BOARD_SPI_MOSI_PIN          72      /* P2.08 */
#define BOARD_SPI_MISO_PIN          73      /* P2.09 */
#define BOARD_SPI_CS_ADS1220_PIN    74      /* P2.10 */
#define BOARD_ADS1220_DRDY_PIN      66      /* P2.02 — active-low data ready   */

/* ── LEDs ──────────────────────────────────────────────────────────────────── */
#define BOARD_LED_GREEN_PIN          4      /* P0.04 */
#define BOARD_LED_RED_PIN           45      /* P1.13 */

/* ── Switch / Button ───────────────────────────────────────────────────────── */
#define BOARD_SWITCH_PIN            41      /* P1.09 */

/* ── External battery supply ───────────────────────────────────────────────── */
#define BOARD_VBAT_EXT_EN_PIN       42      /* P1.10 — high = enable VBAT  */
#define BOARD_VBAT_EXT_nFAULT_PIN  71      /* P2.07 — low  = fault active */

/* ── GPIO pin list (index = GPIO ID) ──────────────────────────────────────── */
#define BOARD_GPIO_PIN_LIST          {BOARD_LED_GREEN_PIN,          \
                                      BOARD_LED_RED_PIN,            \
                                      BOARD_SWITCH_PIN,             \
                                      BOARD_RS485_DE_PIN,           \
                                      BOARD_VBAT_EXT_EN_PIN,        \
                                      BOARD_VBAT_EXT_nFAULT_PIN,   \
                                      BOARD_SPI_CS_ADS1220_PIN,     \
                                      BOARD_ADS1220_DRDY_PIN}

#define BOARD_GPIO_ID_LED_GREEN        0
#define BOARD_GPIO_ID_LED_RED          1
#define BOARD_GPIO_ID_SWITCH           2
#define BOARD_GPIO_ID_RS485_DE         3
#define BOARD_GPIO_ID_VBAT_EXT_EN      4
#define BOARD_GPIO_ID_VBAT_EXT_nFAULT  5
#define BOARD_GPIO_ID_SPI_CS_ADS1220   6
#define BOARD_GPIO_ID_ADS1220_DRDY     7

/* ── LED configuration ─────────────────────────────────────────────────────── */
#define BOARD_LED_ID_LIST            {BOARD_GPIO_ID_LED_GREEN, BOARD_GPIO_ID_LED_RED}
#define BOARD_LED_ACTIVE_LOW         false

/* ── Button configuration ──────────────────────────────────────────────────── */
#define BOARD_BUTTON_ID_LIST         {BOARD_GPIO_ID_SWITCH}
#define BOARD_BUTTON_ACTIVE_LOW      true   /* switch pulls P1.09 to GND */

#endif /* BOARD_SENSORV26_BOARD_H_ */
