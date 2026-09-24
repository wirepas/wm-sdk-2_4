/* ADXL367 low-power 3-axis accelerometer driver (I2C) — Analog Devices
 *
 * I2C address : 0x1D (ASEL=1) or 0x53 (ASEL=0)
 * DEVID_AD    : 0xAD (register 0x00)
 * Datasheet   : ADXL367 Rev. A
 *
 * STUB: bus access, soft-reset, ID check and a raw X/Y/Z read are wired up.
 * Mode/range/ODR and the activity/inactivity (motion-wake) engine are TODO.
 */

#ifndef ADXL367_H_
#define ADXL367_H_

#include <stdint.h>
#include <stdbool.h>

/* ── Register map (subset) ──────────────────────────────────────────────────── */
#define ADXL367_REG_DEVID_AD        0x00u   /* expected: 0xAD */
#define ADXL367_REG_DEVID_MST       0x01u   /* expected: 0x1D */
#define ADXL367_REG_PARTID          0x02u   /* expected: 0xF7 */
#define ADXL367_REG_STATUS          0x0Bu
#define ADXL367_REG_XDATA_H         0x0Eu   /* 14-bit X/Y/Z data block start */
#define ADXL367_REG_TEMP_H          0x14u
#define ADXL367_REG_RESET           0x1Fu   /* write 0x52 ('R') to reset     */
#define ADXL367_REG_FILTER_CTL      0x2Cu   /* range + ODR                   */
#define ADXL367_REG_POWER_CTL       0x2Du   /* measurement mode              */

#define ADXL367_DEVID_AD_VALUE      0xADu
#define ADXL367_RESET_CMD           0x52u

/* ── STATUS bits ────────────────────────────────────────────────────────────── */
#define ADXL367_STATUS_DATA_READY   (1u << 0)
#define ADXL367_STATUS_AWAKE        (1u << 6)

/* ── POWER_CTL measurement mode ────────────────────────────────────────────── */
#define ADXL367_MODE_STANDBY        0x00u
#define ADXL367_MODE_MEASURE        0x02u

/* ── FILTER_CTL range ──────────────────────────────────────────────────────── */
#define ADXL367_RANGE_2G            (0x0u << 6)
#define ADXL367_RANGE_4G            (0x1u << 6)
#define ADXL367_RANGE_8G            (0x2u << 6)

/* ── Raw sample (14-bit signed, right-aligned) ─────────────────────────────── */
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} adxl367_sample_t;

/* ── Configuration ──────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t i2c_addr;   /* ADXL367_I2C_ADDR_HIGH (0x1D) / _LOW (0x53) */
    uint8_t range;      /* ADXL367_RANGE_x */
    bool    pullup;     /* enable MCU internal I2C pull-ups */
} adxl367_cfg_t;

#define ADXL367_I2C_ADDR_HIGH       0x1Du   /* ASEL tied high (this board) */
#define ADXL367_I2C_ADDR_LOW        0x53u

/* ── Return codes ───────────────────────────────────────────────────────────── */
typedef enum
{
    ADXL367_RES_OK = 0,
    ADXL367_RES_I2C_ERR,
    ADXL367_RES_NOT_INITIALIZED,
    ADXL367_RES_WRONG_ID,
} adxl367_res_e;

/* ── API ────────────────────────────────────────────────────────────────────── */

/** Initialize I2C, soft-reset, verify DEVID and enter measurement mode. */
adxl367_res_e ADXL367_init(const adxl367_cfg_t * cfg);

/** Put the device into standby (lowest power). */
adxl367_res_e ADXL367_standby(void);

/** Re-enter measurement mode. */
adxl367_res_e ADXL367_measure(void);

/** True when a new sample is available (STATUS.DATA_READY). */
bool ADXL367_data_ready(void);

/** Read the latest X/Y/Z sample (raw 14-bit signed). */
adxl367_res_e ADXL367_read_xyz(adxl367_sample_t * out);

/** Read a single register (debug / direct access). */
adxl367_res_e ADXL367_read_reg(uint8_t reg, uint8_t * val);

/** Write a single register. */
adxl367_res_e ADXL367_write_reg(uint8_t reg, uint8_t val);

#endif /* ADXL367_H_ */
