/* Bienesis nRF54L15 tag board definition
 *
 * Pin numbering (nrf_gpio.h port-aware):
 *   P0.xx → SW_pin = xx         (port 0, LP domain)
 *   P1.xx → SW_pin = 32 + xx    (port 1, peripheral domain)
 *   P2.xx → SW_pin = 64 + xx    (port 2, main/LP domain)
 *
 * Pin map taken from the board GPIO schematic.
 */

#ifndef BOARD_NRF54L15_TAG_BOARD_H_
#define BOARD_NRF54L15_TAG_BOARD_H_

/* ── RGB LED1 (P2 domain) ──────────────────────────────────────────────────── */
#define BOARD_LED1_RED_PIN          72      /* P2.08 */
#define BOARD_LED1_GREEN_PIN        74      /* P2.10 */
#define BOARD_LED1_BLUE_PIN         73      /* P2.09 */

/* ── RGB LED2 ──────────────────────────────────────────────────────────────── */
#define BOARD_LED2_RED_PIN           2      /* P0.02 */
#define BOARD_LED2_GREEN_PIN         4      /* P0.04 */
#define BOARD_LED2_BLUE_PIN         71      /* P2.07 */

/* ── Buttons (P0 domain) ───────────────────────────────────────────────────── */
#define BOARD_BTN1_PIN               0      /* P0.00 */
#define BOARD_BTN2_PIN               1      /* P0.01 */

/* ── SPI — BMI270 6-axis IMU (Bosch, P1 domain → SPIM2x peripheral domain) ──── */
#define USE_BMI270
#define USE_SPI1                            /* SPIM20 — peripheral domain (P1 pins) */
#define BOARD_SPI_MISO_PIN          37      /* P1.05 */
#define BOARD_SPI_MOSI_PIN          38      /* P1.06 */
#define BOARD_SPI_SCK_PIN           40      /* P1.08 */
#define BOARD_SPI_CS_PIN            39      /* P1.07 — BMI270 chip select */
#define BOARD_SPI_CS_BMI270_PIN     BOARD_SPI_CS_PIN

/* ── TWI / I2C (P1 domain → TWIM2x) ────────────────────────────────────────── */
/* On-board I2C sensors:
 *   BME688  environmental sensor (Bosch)          @ 0x76
 *   ADXL367 low-power accelerometer (Analog Dev.) @ 0x1D
 */
#define USE_I2C1
#define USE_BME688
#define USE_ADXL367
#define BOARD_I2C_SCL_PIN           43      /* P1.11 */
#define BOARD_I2C_SDA_PIN           44      /* P1.12 */
#define BOARD_I2C_BME688_ADDR     0x76
#define BOARD_I2C_ADXL367_ADDR    0x1D

/* ── Antenna select ────────────────────────────────────────────────────────── */
#define BOARD_ANT1_PIN              41      /* P1.09 */
#define BOARD_ANT2_PIN              42      /* P1.10 */

/* ── QSPI flash (P2 domain) ────────────────────────────────────────────────── */
#define BOARD_QSPI_CS_PIN           69      /* P2.05 */
#define BOARD_QSPI_CLK_PIN          65      /* P2.01 */
#define BOARD_QSPI_IO0_PIN          66      /* P2.02 */
#define BOARD_QSPI_IO1_PIN          68      /* P2.04 */
#define BOARD_QSPI_IO2_PIN          67      /* P2.03 */
#define BOARD_QSPI_IO3_PIN          64      /* P2.00 */

/* ── Sensor interrupts / control ───────────────────────────────────────────── */
#define BOARD_ADXL_IRQ_PIN           3      /* P0.03 — ADXL367 accelerometer IRQ */
#define BOARD_BMI_IRQ_PIN           36      /* P1.04 — BMI270 IMU IRQ            */
#define BOARD_BUZZER_PIN            45      /* P1.13 */
#define BOARD_LIGHT_SENSE_PIN       46      /* P1.14 — ambient light sense      */
#define BOARD_LHT_SNS_CTRL_PIN      70      /* P2.06 — light sensor power ctrl  */

/* ── NFC antenna (P1.02/NFC1 / P1.03/NFC2) ─────────────────────────────────── */
/* The nfc_hw_nrf54l15 driver uses the NFCT peripheral directly — no GPIO IDs.
 * The pins are listed here for reference only; do NOT add them to the GPIO list.
 *
 * NFC load capacitance (50 pF target):
 *   Unlike nRF52 (which had NFCT->CTRIM software trim), the nRF54L15 has NO
 *   internal NFC antenna load capacitor. 50 pF must be achieved externally:
 *     - Place C1 = C2 = 100 pF (0402, C0G/NP0) between each NFC pin and GND.
 *     - Tune for resonance at 13.56 MHz with the specific antenna inductance.
 *   The nfc_hw_nrf54l15.c driver needs no change — the NFCT->BIASCFG trim
 *   (internal bias current) is handled automatically from FICR by nrfx. */
#define BOARD_NFC1_PIN              34      /* P1.02/NFC1 */
#define BOARD_NFC2_PIN              35      /* P1.03/NFC2 */
#define USE_NFC                             /* enable nfc_hw_nrf54l15 driver   */

/* ── LED1 RGB aliases (app uses these to express state by colour) ───────────── */
#define LED1_R   BOARD_GPIO_ID_LED1_RED
#define LED1_G   BOARD_GPIO_ID_LED1_GREEN
#define LED1_B   BOARD_GPIO_ID_LED1_BLUE

/* ── GPIO pin list (index = GPIO ID) ──────────────────────────────────────── */
#define BOARD_GPIO_PIN_LIST          {BOARD_LED1_RED_PIN,    \
                                      BOARD_LED1_GREEN_PIN,   \
                                      BOARD_LED1_BLUE_PIN,    \
                                      BOARD_LED2_RED_PIN,     \
                                      BOARD_LED2_GREEN_PIN,   \
                                      BOARD_LED2_BLUE_PIN,    \
                                      BOARD_BTN1_PIN,         \
                                      BOARD_BTN2_PIN,         \
                                      BOARD_BUZZER_PIN,       \
                                      BOARD_LHT_SNS_CTRL_PIN, \
                                      BOARD_ADXL_IRQ_PIN,     \
                                      BOARD_BMI_IRQ_PIN,      \
                                      BOARD_ANT1_PIN,         \
                                      BOARD_ANT2_PIN,         \
                                      BOARD_SPI_CS_BMI270_PIN}

#define BOARD_GPIO_ID_LED1_RED         0
#define BOARD_GPIO_ID_LED1_GREEN       1
#define BOARD_GPIO_ID_LED1_BLUE        2
#define BOARD_GPIO_ID_LED2_RED         3
#define BOARD_GPIO_ID_LED2_GREEN       4
#define BOARD_GPIO_ID_LED2_BLUE        5
#define BOARD_GPIO_ID_BTN1             6
#define BOARD_GPIO_ID_BTN2             7
#define BOARD_GPIO_ID_BUZZER           8
#define BOARD_GPIO_ID_LHT_SNS_CTRL     9
#define BOARD_GPIO_ID_ADXL_IRQ        10
#define BOARD_GPIO_ID_BMI_IRQ         11
#define BOARD_GPIO_ID_ANT1            12
#define BOARD_GPIO_ID_ANT2            13
#define BOARD_GPIO_ID_SPI_CS_BMI270   14

/* ── LED configuration ─────────────────────────────────────────────────────── */
#define BOARD_LED_ID_LIST            {BOARD_GPIO_ID_LED1_RED,   \
                                      BOARD_GPIO_ID_LED1_GREEN, \
                                      BOARD_GPIO_ID_LED1_BLUE,  \
                                      BOARD_GPIO_ID_LED2_RED,   \
                                      BOARD_GPIO_ID_LED2_GREEN, \
                                      BOARD_GPIO_ID_LED2_BLUE}
#define BOARD_LED_ACTIVE_LOW         true   /* RGB LED common-anode: LOW = lit */

/* ── Button configuration ──────────────────────────────────────────────────── */
#define BOARD_BUTTON_ID_LIST         {BOARD_GPIO_ID_BTN1, BOARD_GPIO_ID_BTN2}
#define BOARD_BUTTON_ACTIVE_LOW      true   /* buttons pull to GND */
#define BOARD_BUTTON_INTERNAL_PULL   true

#endif /* BOARD_NRF54L15_TAG_BOARD_H_ */
