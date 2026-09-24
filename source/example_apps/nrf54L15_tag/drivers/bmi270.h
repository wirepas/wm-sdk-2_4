/* BMI270 6-axis IMU driver (SPI) — Bosch
 *
 * Bus       : SPI (4-wire), CS managed by the app via the GPIO HAL.
 * CHIP_ID   : 0x24 (register 0x00)
 * Datasheet : BST-BMI270-DS000
 *
 * STUB: SPI plumbing, CS handling, the SPI-mode "dummy read" wake and the
 * CHIP_ID check are implemented. The mandatory ~8 KB config-file upload that
 * BMI270 needs before normal operation is left as TODO (BMI270_load_config).
 * Until that is done, only CHIP_ID / register access is reliable.
 */

#ifndef BMI270_H_
#define BMI270_H_

#include <stdint.h>
#include <stdbool.h>

/* ── Register map (subset) ──────────────────────────────────────────────────── */
#define BMI270_REG_CHIP_ID          0x00u   /* expected: 0x24 */
#define BMI270_REG_STATUS           0x03u
#define BMI270_REG_ACC_DATA         0x0Cu   /* ACC_X_LSB … GYR_Z_MSB (12 bytes) */
#define BMI270_REG_INTERNAL_STATUS  0x21u   /* 0x01 = config loaded OK          */
#define BMI270_REG_INIT_CTRL        0x59u
#define BMI270_REG_INIT_ADDR_0      0x5Bu
#define BMI270_REG_INIT_ADDR_1      0x5Cu
#define BMI270_REG_INIT_DATA        0x5Eu
#define BMI270_REG_PWR_CONF         0x7Cu
#define BMI270_REG_PWR_CTRL         0x7Du
#define BMI270_REG_CMD              0x7Eu   /* 0xB6 = soft reset */

#define BMI270_CHIP_ID_VALUE        0x24u
#define BMI270_CMD_SOFT_RESET       0xB6u

/* SPI read/write direction bit (bit 7 of the register address). */
#define BMI270_SPI_READ             0x80u

/* ── Raw 6-axis sample (each axis 16-bit signed, little-endian) ─────────────── */
typedef struct
{
    int16_t acc_x, acc_y, acc_z;
    int16_t gyr_x, gyr_y, gyr_z;
} bmi270_sample_t;

/* ── Configuration ──────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t  cs_gpio_id;    /* BOARD_GPIO_ID_SPI_CS_BMI270 */
    uint32_t spi_clock_hz;  /* e.g. 8000000 */
} bmi270_cfg_t;

/* ── Return codes ───────────────────────────────────────────────────────────── */
typedef enum
{
    BMI270_RES_OK = 0,
    BMI270_RES_SPI_ERR,
    BMI270_RES_NOT_INITIALIZED,
    BMI270_RES_WRONG_ID,
    BMI270_RES_CONFIG_ERR,
    BMI270_RES_NOT_IMPLEMENTED,
} bmi270_res_e;

/* ── API ────────────────────────────────────────────────────────────────────── */

/** Initialize SPI + CS, switch the chip to SPI mode, reset and verify CHIP_ID. */
bmi270_res_e BMI270_init(const bmi270_cfg_t * cfg);

/**
 * Upload the Bosch config blob and enable accel + gyro.
 * STUB: returns BMI270_RES_NOT_IMPLEMENTED until the config array is supplied.
 */
bmi270_res_e BMI270_load_config(void);

/** True when new data is available (STATUS.drdy bits). */
bool BMI270_data_ready(void);

/** Read the latest accel + gyro sample (raw, little-endian). */
bmi270_res_e BMI270_read(bmi270_sample_t * out);

/** Read a single register (debug / direct access). */
bmi270_res_e BMI270_read_reg(uint8_t reg, uint8_t * val);

/** Write a single register. */
bmi270_res_e BMI270_write_reg(uint8_t reg, uint8_t val);

#endif /* BMI270_H_ */
