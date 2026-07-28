/* LIS2DW12 3-axis accelerometer driver (I2C) */

#include <stddef.h>

#include "lis2dw.h"
#include "i2c.h"

#define I2C_CLOCK_HZ    400000u

static bool     m_initialized = false;
static uint8_t  m_i2c_addr;
static uint8_t  m_ctrl1;   /* saved to restore ODR on wake-up */

/* ── Internal helpers ───────────────────────────────────────────────────────── */

static lis2dw_res_e reg_write(uint8_t reg, uint8_t val)
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
           ? LIS2DW_RES_OK : LIS2DW_RES_I2C_ERR;
}

static lis2dw_res_e reg_read(uint8_t reg, uint8_t * val)
{
    i2c_xfer_t xfer = {
        .address    = m_i2c_addr,
        .write_ptr  = &reg,
        .write_size = 1,
        .read_ptr   = val,
        .read_size  = 1,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? LIS2DW_RES_OK : LIS2DW_RES_I2C_ERR;
}

/* Burst read n bytes starting at reg (auto-increment with MSB of addr set). */
static lis2dw_res_e reg_read_burst(uint8_t reg, uint8_t * buf, uint8_t n)
{
    /* LIS2DW12 auto-increments when bit 7 of the sub-address is set. */
    uint8_t addr = reg | 0x80u;
    i2c_xfer_t xfer = {
        .address    = m_i2c_addr,
        .write_ptr  = &addr,
        .write_size = 1,
        .read_ptr   = buf,
        .read_size  = n,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? LIS2DW_RES_OK : LIS2DW_RES_I2C_ERR;
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

lis2dw_res_e LIS2DW_init(const lis2dw_cfg_t * cfg)
{
    i2c_conf_t i2c_conf = {
        .clock  = I2C_CLOCK_HZ,
        .pullup = cfg->pullup,
    };
    i2c_res_e res = I2C_init(&i2c_conf);
    if (res != I2C_RES_OK && res != I2C_RES_ALREADY_INITIALIZED)
        return LIS2DW_RES_I2C_ERR;

    m_i2c_addr = cfg->i2c_addr;

    uint8_t who;
    if (reg_read(LIS2DW_REG_WHO_AM_I, &who) != LIS2DW_RES_OK)
        return LIS2DW_RES_I2C_ERR;
    if (who != LIS2DW_WHO_AM_I_VALUE)
        return LIS2DW_RES_WRONG_ID;

    /* Software reset then re-enable */
    lis2dw_res_e r;
    r = reg_write(LIS2DW_REG_CTRL2, 0x40u);   /* SOFT_RESET */
    if (r != LIS2DW_RES_OK) return r;

    /* CTRL2: BDU + IF_ADD_INC */
    r = reg_write(LIS2DW_REG_CTRL2, 0x08u | 0x04u);
    if (r != LIS2DW_RES_OK) return r;

    /* CTRL6: full-scale + low-noise */
    uint8_t ctrl6 = cfg->full_scale | (cfg->low_noise ? LIS2DW_LOW_NOISE : 0u);
    r = reg_write(LIS2DW_REG_CTRL6, ctrl6);
    if (r != LIS2DW_RES_OK) return r;

    /* CTRL1: ODR + MODE */
    m_ctrl1 = cfg->odr | (cfg->mode & 0x03u);
    r = reg_write(LIS2DW_REG_CTRL1, m_ctrl1);
    if (r != LIS2DW_RES_OK) return r;

    m_initialized = true;
    return LIS2DW_RES_OK;
}

lis2dw_res_e LIS2DW_power_down(void)
{
    if (!m_initialized) return LIS2DW_RES_NOT_INITIALIZED;
    return reg_write(LIS2DW_REG_CTRL1, LIS2DW_ODR_PWRDN | (m_ctrl1 & 0x03u));
}

lis2dw_res_e LIS2DW_wake_up(void)
{
    if (!m_initialized) return LIS2DW_RES_NOT_INITIALIZED;
    return reg_write(LIS2DW_REG_CTRL1, m_ctrl1);
}

bool LIS2DW_data_ready(void)
{
    uint8_t s = 0;
    reg_read(LIS2DW_REG_STATUS, &s);
    return (s & LIS2DW_STATUS_DRDY) != 0u;
}

lis2dw_res_e LIS2DW_read_xyz(lis2dw_sample_t * out)
{
    if (!m_initialized) return LIS2DW_RES_NOT_INITIALIZED;

    uint8_t raw[6];
    lis2dw_res_e r = reg_read_burst(LIS2DW_REG_OUT_X_L, raw, 6u);
    if (r != LIS2DW_RES_OK) return r;

    /* 14-bit left-aligned in 16-bit register pair (little-endian) */
    out->x = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
    out->y = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
    out->z = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));

    return LIS2DW_RES_OK;
}

lis2dw_res_e LIS2DW_read_reg(uint8_t reg, uint8_t * val)
{
    if (!m_initialized) return LIS2DW_RES_NOT_INITIALIZED;
    return reg_read(reg, val);
}

lis2dw_res_e LIS2DW_write_reg(uint8_t reg, uint8_t val)
{
    if (!m_initialized) return LIS2DW_RES_NOT_INITIALIZED;
    return reg_write(reg, val);
}
