/* ADS1220 24-bit ADC driver (SPI, Mode 1: CPOL=0 CPHA=1)
 *
 * Datasheet: SBAS501E — Texas Instruments
 * CS is managed by the driver via BOARD_GPIO_ID_SPI_CS_ADS1220.
 * DRDY is monitored via BOARD_GPIO_ID_ADS1220_DRDY (active-low).
 */

#ifndef ADS1220_H_
#define ADS1220_H_

#include <stdint.h>
#include <stdbool.h>

/* ── SPI commands ───────────────────────────────────────────────────────────── */
#define ADS1220_CMD_RESET       0x06u
#define ADS1220_CMD_START       0x08u
#define ADS1220_CMD_POWERDOWN   0x02u
#define ADS1220_CMD_RDATA       0x10u
#define ADS1220_CMD_RREG(r,n)  (0x20u | (((r) & 0x3u) << 2) | (((n)-1u) & 0x3u))
#define ADS1220_CMD_WREG(r,n)  (0x40u | (((r) & 0x3u) << 2) | (((n)-1u) & 0x3u))

/* ── Configuration register 0 (REG0) ───────────────────────────────────────── */
#define ADS1220_MUX_SHIFT       4u
#define ADS1220_MUX_AIN0_AIN1   (0x0u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN0_AIN2   (0x1u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN0_AIN3   (0x2u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN1_AIN2   (0x3u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN1_AIN3   (0x4u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN2_AIN3   (0x5u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN1_AIN0   (0x6u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN3_AIN2   (0x7u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN0_AVSS   (0x8u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN1_AVSS   (0x9u << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN2_AVSS   (0xAu << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_AIN3_AVSS   (0xBu << ADS1220_MUX_SHIFT)
#define ADS1220_MUX_VMID        (0xCu << ADS1220_MUX_SHIFT)  /* (AVDD-AVSS)/4 */
#define ADS1220_MUX_AVDD        (0xDu << ADS1220_MUX_SHIFT)  /* supply monitor */
#define ADS1220_MUX_SHORT       (0xEu << ADS1220_MUX_SHIFT)  /* shorted inputs */

#define ADS1220_GAIN_SHIFT      1u
#define ADS1220_GAIN_1          (0u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_2          (1u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_4          (2u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_8          (3u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_16         (4u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_32         (5u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_64         (6u << ADS1220_GAIN_SHIFT)
#define ADS1220_GAIN_128        (7u << ADS1220_GAIN_SHIFT)

#define ADS1220_PGA_BYPASS      (1u << 0)   /* bypass PGA (single-ended inputs) */

/* ── Configuration register 1 (REG1) ───────────────────────────────────────── */
#define ADS1220_DR_SHIFT        5u
#define ADS1220_DR_20SPS        (0u << ADS1220_DR_SHIFT)
#define ADS1220_DR_45SPS        (1u << ADS1220_DR_SHIFT)
#define ADS1220_DR_90SPS        (2u << ADS1220_DR_SHIFT)
#define ADS1220_DR_175SPS       (3u << ADS1220_DR_SHIFT)
#define ADS1220_DR_330SPS       (4u << ADS1220_DR_SHIFT)
#define ADS1220_DR_600SPS       (5u << ADS1220_DR_SHIFT)
#define ADS1220_DR_1000SPS      (6u << ADS1220_DR_SHIFT)

#define ADS1220_MODE_NORMAL     (0u << 3)   /* normal mode   */
#define ADS1220_MODE_DUTY       (1u << 3)   /* duty-cycle    */
#define ADS1220_MODE_TURBO      (2u << 3)   /* turbo         */

#define ADS1220_CM_CONTINUOUS   (1u << 2)   /* continuous conversion */
#define ADS1220_TS_ENABLE       (1u << 1)   /* internal temperature sensor */
#define ADS1220_BCS_ENABLE      (1u << 0)   /* burnout current source */

/* ── Configuration register 2 (REG2) ───────────────────────────────────────── */
#define ADS1220_VREF_INT        (0u << 6)   /* internal 2.048 V reference */
#define ADS1220_VREF_EXT_REFP0  (1u << 6)   /* external REFP0/REFN0 */
#define ADS1220_VREF_EXT_AIN01  (2u << 6)   /* AIN0/AIN3 as reference */
#define ADS1220_VREF_AVDD       (3u << 6)   /* AVDD/AVSS */

#define ADS1220_FIR_50_60HZ     (0u << 4)
#define ADS1220_FIR_50HZ        (1u << 4)
#define ADS1220_FIR_60HZ        (2u << 4)

#define ADS1220_PSW_CLOSE       (1u << 3)   /* low-side power switch */

#define ADS1220_IDAC_OFF        (0u << 0)
#define ADS1220_IDAC_10UA       (1u << 0)
#define ADS1220_IDAC_50UA       (2u << 0)
#define ADS1220_IDAC_100UA      (3u << 0)
#define ADS1220_IDAC_250UA      (4u << 0)
#define ADS1220_IDAC_500UA      (5u << 0)
#define ADS1220_IDAC_1000UA     (6u << 0)
#define ADS1220_IDAC_1500UA     (7u << 0)

/* ── Configuration register 3 (REG3) ───────────────────────────────────────── */
#define ADS1220_I1MUX_DISABLED  (0u << 5)
#define ADS1220_I1MUX_AIN0      (1u << 5)
#define ADS1220_I1MUX_AIN1      (2u << 5)
#define ADS1220_I1MUX_AIN2      (3u << 5)
#define ADS1220_I1MUX_AIN3      (4u << 5)
#define ADS1220_I1MUX_REFP0     (5u << 5)
#define ADS1220_I1MUX_REFN0     (6u << 5)

#define ADS1220_I2MUX_DISABLED  (0u << 2)
#define ADS1220_I2MUX_AIN0      (1u << 2)
#define ADS1220_I2MUX_AIN1      (2u << 2)
#define ADS1220_I2MUX_AIN2      (3u << 2)
#define ADS1220_I2MUX_AIN3      (4u << 2)
#define ADS1220_I2MUX_REFP0     (5u << 2)
#define ADS1220_I2MUX_REFN0     (6u << 2)

/* ── Register snapshot ──────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t reg[4];     /* reg[0]=REG0 … reg[3]=REG3 */
} ads1220_regs_t;

/* ── Return codes ───────────────────────────────────────────────────────────── */
typedef enum
{
    ADS1220_RES_OK = 0,
    ADS1220_RES_SPI_ERR,
    ADS1220_RES_NOT_INITIALIZED,
    ADS1220_RES_NOT_READY,      /* DRDY not asserted within timeout */
} ads1220_res_e;

/* ── API ────────────────────────────────────────────────────────────────────── */

/**
 * Initialize SPI and reset + configure the ADS1220.
 * cs_gpio_id   : BOARD_GPIO_ID_SPI_CS_ADS1220
 * drdy_gpio_id : BOARD_GPIO_ID_ADS1220_DRDY
 */
ads1220_res_e ADS1220_init(uint8_t cs_gpio_id,
                            uint8_t drdy_gpio_id,
                            const ads1220_regs_t * cfg);

/** Send POWERDOWN command. */
ads1220_res_e ADS1220_power_down(void);

/** Send START/SYNC command to begin a conversion (single-shot or continuous). */
ads1220_res_e ADS1220_start(void);

/**
 * Poll DRDY and read the 24-bit result (blocking, up to timeout_ms).
 * result is a signed 24-bit value sign-extended into int32_t.
 */
ads1220_res_e ADS1220_read_result(int32_t * result, uint32_t timeout_ms);

/** Write all 4 configuration registers at once. */
ads1220_res_e ADS1220_write_regs(const ads1220_regs_t * regs);

/** Read all 4 configuration registers. */
ads1220_res_e ADS1220_read_regs(ads1220_regs_t * regs);

/**
 * Change the input multiplexer without reinitialising the chip.
 * mux must be one of the ADS1220_MUX_xxx constants (value already shifted
 * to bits [7:4] of REG0, e.g. ADS1220_MUX_AIN1_AVSS).
 */
ads1220_res_e ADS1220_set_mux(uint8_t mux);

/** Convenience: trigger a single-shot conversion and return the result. */
ads1220_res_e ADS1220_read_single(int32_t * result, uint32_t timeout_ms);

#endif /* ADS1220_H_ */
