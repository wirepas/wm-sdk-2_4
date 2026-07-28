/* LIS2DW12 3-axis accelerometer driver (I2C)
 *
 * I2C address: 0x18 (SA0=GND) or 0x19 (SA0=VCC)
 * Datasheet  : DS12441 rev 5 — STMicroelectronics
 */

#ifndef LIS2DW_H_
#define LIS2DW_H_

#include <stdint.h>
#include <stdbool.h>

/* ── Register map ───────────────────────────────────────────────────────────── */
#define LIS2DW_REG_OUT_T_L          0x0Du
#define LIS2DW_REG_OUT_T_H          0x0Eu
#define LIS2DW_REG_WHO_AM_I         0x0Fu   /* expected: 0x44 */
#define LIS2DW_REG_CTRL1            0x20u
#define LIS2DW_REG_CTRL2            0x21u
#define LIS2DW_REG_CTRL3            0x22u
#define LIS2DW_REG_CTRL4_INT1       0x23u
#define LIS2DW_REG_CTRL5_INT2       0x24u
#define LIS2DW_REG_CTRL6            0x25u
#define LIS2DW_REG_STATUS           0x27u
#define LIS2DW_REG_OUT_X_L          0x28u
#define LIS2DW_REG_OUT_X_H          0x29u
#define LIS2DW_REG_OUT_Y_L          0x2Au
#define LIS2DW_REG_OUT_Y_H          0x2Bu
#define LIS2DW_REG_OUT_Z_L          0x2Cu
#define LIS2DW_REG_OUT_Z_H          0x2Du
#define LIS2DW_REG_FIFO_CTRL        0x2Eu
#define LIS2DW_REG_WAKE_UP_THS      0x34u
#define LIS2DW_REG_WAKE_UP_DUR      0x35u
#define LIS2DW_REG_FREE_FALL        0x36u
#define LIS2DW_REG_WAKE_UP_SRC      0x38u
#define LIS2DW_REG_TAP_SRC          0x39u
#define LIS2DW_REG_SIXD_SRC         0x3Au
#define LIS2DW_REG_ALL_INT_SRC      0x3Bu
#define LIS2DW_REG_CTRL7            0x3Fu

/* ── CTRL1 — ODR and mode ───────────────────────────────────────────────────── */
#define LIS2DW_ODR_SHIFT            4u
#define LIS2DW_ODR_PWRDN            (0x0u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_1HZ6             (0x1u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_12HZ5            (0x2u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_25HZ             (0x3u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_50HZ             (0x4u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_100HZ            (0x5u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_200HZ            (0x6u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_400HZ            (0x7u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_800HZ            (0x8u << LIS2DW_ODR_SHIFT)
#define LIS2DW_ODR_1600HZ           (0x9u << LIS2DW_ODR_SHIFT)

#define LIS2DW_MODE_LP1             0x00u   /* low-power mode 1 (12-bit) */
#define LIS2DW_MODE_LP2             0x01u   /* low-power mode 2 (14-bit) */
#define LIS2DW_MODE_LP3             0x02u   /* low-power mode 3 (14-bit) */
#define LIS2DW_MODE_LP4             0x03u   /* low-power mode 4 (14-bit) */
#define LIS2DW_MODE_HIGH_PERF       0x04u   /* high-performance (14-bit) */

/* ── CTRL6 — full-scale and filter ─────────────────────────────────────────── */
#define LIS2DW_FS_2G                (0x0u << 4)
#define LIS2DW_FS_4G                (0x1u << 4)
#define LIS2DW_FS_8G                (0x2u << 4)
#define LIS2DW_FS_16G               (0x3u << 4)
#define LIS2DW_LOW_NOISE            (1u << 2)

/* ── STATUS bits ────────────────────────────────────────────────────────────── */
#define LIS2DW_STATUS_DRDY          (1u << 0)
#define LIS2DW_STATUS_FF_IA         (1u << 1)
#define LIS2DW_STATUS_6D_IA         (1u << 2)
#define LIS2DW_STATUS_SINGLE_TAP    (1u << 3)
#define LIS2DW_STATUS_DOUBLE_TAP    (1u << 4)
#define LIS2DW_STATUS_SLEEP         (1u << 5)
#define LIS2DW_STATUS_WU_IA         (1u << 6)
#define LIS2DW_STATUS_FIFO_THS      (1u << 7)

/* ── WHO_AM_I expected value ────────────────────────────────────────────────── */
#define LIS2DW_WHO_AM_I_VALUE       0x44u

/* ── Raw sample (14-bit left-aligned, 2-byte little-endian) ────────────────── */
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} lis2dw_sample_t;

/* ── Configuration ──────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t i2c_addr;   /* LIS2DW_I2C_ADDR_SA0_LOW (0x18) or _HIGH (0x19) */
    uint8_t odr;        /* LIS2DW_ODR_x */
    uint8_t mode;       /* LIS2DW_MODE_x */
    uint8_t full_scale; /* LIS2DW_FS_x */
    bool    low_noise;
    bool    pullup;     /* true = enable MCU internal I2C pull-ups */
} lis2dw_cfg_t;

#define LIS2DW_I2C_ADDR_SA0_LOW     0x18u
#define LIS2DW_I2C_ADDR_SA0_HIGH    0x19u

/* ── Return codes ───────────────────────────────────────────────────────────── */
typedef enum
{
    LIS2DW_RES_OK = 0,
    LIS2DW_RES_I2C_ERR,
    LIS2DW_RES_NOT_INITIALIZED,
    LIS2DW_RES_WRONG_ID,
} lis2dw_res_e;

/* ── API ────────────────────────────────────────────────────────────────────── */

/**
 * Initialize I2C and configure the LIS2DW12.
 * Verifies WHO_AM_I; returns LIS2DW_RES_WRONG_ID if the chip does not respond.
 */
lis2dw_res_e LIS2DW_init(const lis2dw_cfg_t * cfg);

/** Put the chip into power-down mode (ODR = 0). */
lis2dw_res_e LIS2DW_power_down(void);

/** Restore the ODR set during init. */
lis2dw_res_e LIS2DW_wake_up(void);

/** Return true when new data is available (STATUS.DRDY). */
bool LIS2DW_data_ready(void);

/**
 * Read the latest X/Y/Z acceleration sample.
 * Values are raw 14-bit signed integers (left-aligned in int16_t).
 * To convert: accel_mg = (raw >> 2) × sensitivity_mg_per_lsb
 * where sensitivity depends on full-scale and mode (see datasheet Table 3).
 */
lis2dw_res_e LIS2DW_read_xyz(lis2dw_sample_t * out);

/** Read a single register (for direct register access / debug). */
lis2dw_res_e LIS2DW_read_reg(uint8_t reg, uint8_t * val);

/** Write a single register. */
lis2dw_res_e LIS2DW_write_reg(uint8_t reg, uint8_t val);

#endif /* LIS2DW_H_ */
