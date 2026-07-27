/* ADXL367 low-power 3-axis accelerometer driver (I2C) — Analog Devices
 *
 * STUB implementation: bus access, reset, ID check and raw read are
 * functional. Motion-wake / activity engine config is left as TODO.
 */

#include <stddef.h>

#include "adxl367.h"
#include "i2c.h"

#define I2C_CLOCK_HZ    400000u

static bool    m_initialized = false;
static uint8_t m_i2c_addr;

/* ── Internal helpers ───────────────────────────────────────────────────────── */

static adxl367_res_e reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_xfer_t xfer = {
        .address    = m_i2c_addr,
        .write_ptr  = buf,
        .write_size = 2,
        .read_ptr   = NULL,
        .read_size  = 0,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? ADXL367_RES_OK : ADXL367_RES_I2C_ERR;
}

static adxl367_res_e reg_read(uint8_t reg, uint8_t * val)
{
    i2c_xfer_t xfer = {
        .address    = m_i2c_addr,
        .write_ptr  = &reg,
        .write_size = 1,
        .read_ptr   = val,
        .read_size  = 1,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? ADXL367_RES_OK : ADXL367_RES_I2C_ERR;
}

static adxl367_res_e reg_read_burst(uint8_t reg, uint8_t * buf, uint8_t n)
{
    /* ADXL367 auto-increments the register pointer on consecutive reads. */
    i2c_xfer_t xfer = {
        .address    = m_i2c_addr,
        .write_ptr  = &reg,
        .write_size = 1,
        .read_ptr   = buf,
        .read_size  = n,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? ADXL367_RES_OK : ADXL367_RES_I2C_ERR;
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

adxl367_res_e ADXL367_init(const adxl367_cfg_t * cfg)
{
    i2c_conf_t i2c_conf = {
        .clock  = I2C_CLOCK_HZ,
        .pullup = cfg->pullup,
    };
    i2c_res_e res = I2C_init(&i2c_conf);
    if (res != I2C_RES_OK && res != I2C_RES_ALREADY_INITIALIZED)
        return ADXL367_RES_I2C_ERR;

    m_i2c_addr = cfg->i2c_addr;

    uint8_t id;
    if (reg_read(ADXL367_REG_DEVID_AD, &id) != ADXL367_RES_OK)
        return ADXL367_RES_I2C_ERR;
    if (id != ADXL367_DEVID_AD_VALUE)
        return ADXL367_RES_WRONG_ID;

    /* Soft reset (must be in standby; reset returns it to standby). */
    adxl367_res_e r = reg_write(ADXL367_REG_RESET, ADXL367_RESET_CMD);
    if (r != ADXL367_RES_OK) return r;

    /* Range (ODR/half-bandwidth left at reset defaults for now). */
    r = reg_write(ADXL367_REG_FILTER_CTL, cfg->range);
    if (r != ADXL367_RES_OK) return r;

    /* TODO: configure activity/inactivity thresholds + INT map for the
     *       ADXL_IRQ (P0.03) motion-wake line. */

    m_initialized = true;
    return ADXL367_measure();
}

adxl367_res_e ADXL367_standby(void)
{
    if (!m_initialized) return ADXL367_RES_NOT_INITIALIZED;
    return reg_write(ADXL367_REG_POWER_CTL, ADXL367_MODE_STANDBY);
}

adxl367_res_e ADXL367_measure(void)
{
    if (!m_initialized) return ADXL367_RES_NOT_INITIALIZED;
    return reg_write(ADXL367_REG_POWER_CTL, ADXL367_MODE_MEASURE);
}

bool ADXL367_data_ready(void)
{
    uint8_t s = 0;
    if (reg_read(ADXL367_REG_STATUS, &s) != ADXL367_RES_OK)
        return false;
    return (s & ADXL367_STATUS_DATA_READY) != 0u;
}

adxl367_res_e ADXL367_read_xyz(adxl367_sample_t * out)
{
    if (!m_initialized) return ADXL367_RES_NOT_INITIALIZED;

    uint8_t raw[6];
    adxl367_res_e r = reg_read_burst(ADXL367_REG_XDATA_H, raw, 6u);
    if (r != ADXL367_RES_OK) return r;

    /* Each axis: 14-bit, big-endian, left-justified in the [H:L] pair.
     * Shift right by 2 to right-align the signed 14-bit value. */
    out->x = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]) >> 2;
    out->y = (int16_t)(((uint16_t)raw[2] << 8) | raw[3]) >> 2;
    out->z = (int16_t)(((uint16_t)raw[4] << 8) | raw[5]) >> 2;

    return ADXL367_RES_OK;
}

adxl367_res_e ADXL367_read_reg(uint8_t reg, uint8_t * val)
{
    if (!m_initialized) return ADXL367_RES_NOT_INITIALIZED;
    return reg_read(reg, val);
}

adxl367_res_e ADXL367_write_reg(uint8_t reg, uint8_t val)
{
    if (!m_initialized) return ADXL367_RES_NOT_INITIALIZED;
    return reg_write(reg, val);
}
