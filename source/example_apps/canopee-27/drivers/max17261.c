/* MAX17261 ModelGauge m5 fuel-gauge driver — see max17261.h */

#include <stddef.h>
#include "max17261.h"
#include "i2c.h"

#define I2C_CLOCK_HZ        100000u

/* Register map (16-bit each, little-endian) */
#define MAX17261_REG_STATUS   0x00u
#define MAX17261_REG_REPCAP   0x05u
#define MAX17261_REG_REPSOC   0x06u
#define MAX17261_REG_TEMP     0x08u
#define MAX17261_REG_VCELL    0x09u
#define MAX17261_REG_CURRENT  0x0Au
#define MAX17261_REG_DEVNAME  0x21u   /* used only as a presence probe */

/* Fixed-point LSB values (datasheet §8.1) */
#define MAX17261_VCELL_UV       78.125f   /* VCell LSB = 78.125 µV                */
#define MAX17261_SOC_LSB      (1.0f/256)  /* RepSOC LSB = 1/256 %                 */
#define MAX17261_TEMP_LSB     (1.0f/256)  /* Temp   LSB = 1/256 °C (signed)       */
#define MAX17261_CURRENT_UV     1.5625f   /* Current LSB = 1.5625 µV / Rsense     */

static bool     m_initialized = false;
static uint16_t m_rsense_mohm = 10u;

static max17261_res_e reg_read16(uint8_t reg, uint16_t * val)
{
    uint8_t rx[2];
    i2c_xfer_t xfer = {
        .address    = MAX17261_I2C_ADDR,
        .write_ptr  = &reg,
        .write_size = 1u,
        .read_ptr   = rx,
        .read_size  = 2u,
    };
    if (I2C_transfer(&xfer, NULL) != I2C_RES_OK)
        return MAX17261_RES_I2C_ERR;
    *val = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);   /* little-endian */
    return MAX17261_RES_OK;
}

max17261_res_e MAX17261_init(uint16_t rsense_mohm, int pullup)
{
    i2c_conf_t i2c_conf = {
        .clock  = I2C_CLOCK_HZ,
        .pullup = (bool)pullup,
    };
    i2c_res_e res = I2C_init(&i2c_conf);
    if (res != I2C_RES_OK && res != I2C_RES_ALREADY_INITIALIZED)
        return MAX17261_RES_I2C_ERR;

    m_rsense_mohm = (rsense_mohm != 0u) ? rsense_mohm : 10u;

    /* Presence probe: the gauge must acknowledge a register read. */
    uint16_t dummy;
    if (reg_read16(MAX17261_REG_STATUS, &dummy) != MAX17261_RES_OK)
        return MAX17261_RES_I2C_ERR;

    m_initialized = true;
    return MAX17261_RES_OK;
}

max17261_res_e MAX17261_read(max17261_data_t * out)
{
    if (!m_initialized) return MAX17261_RES_NOT_INITIALIZED;
    if (out == NULL)    return MAX17261_RES_I2C_ERR;

    uint16_t vcell, repsoc, temp, current, status;
    max17261_res_e r;

    if ((r = reg_read16(MAX17261_REG_VCELL,   &vcell))   != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_REPSOC,  &repsoc))  != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_TEMP,    &temp))    != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_CURRENT, &current)) != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_STATUS,  &status))  != MAX17261_RES_OK) return r;

    out->voltage_mv = (uint16_t)((float)vcell * MAX17261_VCELL_UV / 1000.0f);
    out->soc_pct    = (float)repsoc * MAX17261_SOC_LSB;
    out->temp_c     = (float)(int16_t)temp * MAX17261_TEMP_LSB;
    /* Current LSB = 1.5625 µV / Rsense → mA = raw * 1.5625 / Rsense_mΩ. */
    out->current_ma = (int16_t)((float)(int16_t)current
                                * MAX17261_CURRENT_UV / (float)m_rsense_mohm);
    out->status     = status;
    return MAX17261_RES_OK;
}
