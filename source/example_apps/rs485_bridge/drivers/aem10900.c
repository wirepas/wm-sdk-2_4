/* AEM10900 PMIC driver — e-peas energy harvesting IC (I2C) */

#include <stddef.h>

#include "aem10900.h"
#include "i2c.h"

#define AEM10900_I2C_ADDR   0x2Du
#define I2C_CLOCK_HZ        400000u
#define SYNCBUSY_RETRIES    100u

static bool m_initialized = false;

/* ── Source voltage lookup table (raw → millivolts) ────────────────────────── */
/* Index = SRC register value; from e-peas AEM10900 datasheet Table 5.
 * Entries cover raw codes 0x00–0x3F. Gaps filled with 0. */
static const uint16_t k_src_mv_lut[64u] = {
       0,    0,    0,    0,    0,    0,  113,  140,
     168,  196,  224,  253,  283,  313,  344,  376,
     408,  441,  474,  508,  542,  577,  613,  649,
     685,  722,  759,  797,  835,  874,  913,  953,
     993, 1034, 1075, 1117, 1159, 1202, 1245, 1289,
    1333, 1378, 1423, 1469, 1485,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
};

/* ── Internal helpers ───────────────────────────────────────────────────────── */

static aem10900_res_e reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_xfer_t xfer = {
        .address    = AEM10900_I2C_ADDR,
        .write_ptr  = buf,
        .write_size = 2u,
        .read_ptr   = NULL,
        .read_size  = 0u,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? AEM10900_RES_OK : AEM10900_RES_I2C_ERR;
}

static aem10900_res_e reg_read(uint8_t reg, uint8_t * val)
{
    i2c_xfer_t xfer = {
        .address    = AEM10900_I2C_ADDR,
        .write_ptr  = &reg,
        .write_size = 1u,
        .read_ptr   = val,
        .read_size  = 1u,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? AEM10900_RES_OK : AEM10900_RES_I2C_ERR;
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

aem10900_res_e AEM10900_init(const aem10900_cfg_t * cfg)
{
    i2c_conf_t i2c_conf = {
        .clock  = I2C_CLOCK_HZ,
        .pullup = false,    /* rely on external pull-ups */
    };
    i2c_res_e res = I2C_init(&i2c_conf);
    if (res != I2C_RES_OK && res != I2C_RES_ALREADY_INITIALIZED)
        return AEM10900_RES_I2C_ERR;

    /* Burst write registers 0x01–0x0B (11 bytes) in one I2C transaction. */
    uint8_t reg_start = AEM10900_REG_MPPTCFG;
    uint8_t payload[12];    /* [0] = reg address, [1..11] = reg values */

    payload[0]  = reg_start;
    payload[1]  = (uint8_t)(cfg->mppt_timing << 4u) | (cfg->mppt_ratio & 0x0Fu);
    payload[2]  = cfg->vovdis_raw  & 0x3Fu;
    payload[3]  = cfg->vovch_raw   & 0x3Fu;
    payload[4]  = cfg->tempcold_raw;
    payload[5]  = cfg->temphot_raw;
    payload[6]  = cfg->pwr_mask & 0x0Fu;
    payload[7]  = (uint8_t)((cfg->sleep_srcthresh & 0x07u) << 1u)
                | (cfg->sleep_enable ? AEM10900_SLEEP_EN : 0u);
    payload[8]  = (uint8_t)(cfg->stomon_rate & 0x07u);
    payload[9]  = (uint8_t)((cfg->apm_window & 0x03u) << 2u)
                | (uint8_t)((cfg->apm_mode   & 0x01u) << 1u)
                | (cfg->apm_enable ? AEM10900_APM_EN : 0u);
    payload[10] = cfg->irq_mask & 0x7Fu;
    /* CTRL: UPDATE=I2C — chip uses I2C registers as config source */
    payload[11] = AEM10900_CTRL_UPDATE_I2C;

    i2c_xfer_t xfer = {
        .address    = AEM10900_I2C_ADDR,
        .write_ptr  = payload,
        .write_size = sizeof(payload),
        .read_ptr   = NULL,
        .read_size  = 0u,
    };
    if (I2C_transfer(&xfer, NULL) != I2C_RES_OK)
        return AEM10900_RES_I2C_ERR;

    /* Poll CTRL.SYNCBUSY until the EEPROM write completes. */
    uint8_t ctrl;
    for (uint32_t i = 0u; i < SYNCBUSY_RETRIES; i++)
    {
        if (reg_read(AEM10900_REG_CTRL, &ctrl) != AEM10900_RES_OK)
            return AEM10900_RES_I2C_ERR;
        if ((ctrl & AEM10900_CTRL_SYNCBUSY) == 0u)
        {
            m_initialized = true;
            return AEM10900_RES_OK;
        }
    }
    return AEM10900_RES_SYNC_TIMEOUT;
}

aem10900_res_e AEM10900_get_status(uint8_t * status)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    return reg_read(AEM10900_REG_STATUS, status);
}

aem10900_res_e AEM10900_get_irqflg(uint8_t * flags)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    return reg_read(AEM10900_REG_IRQFLG, flags);
}

aem10900_res_e AEM10900_read_storage_voltage(float * voltage_v)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    uint8_t raw;
    aem10900_res_e r = reg_read(AEM10900_REG_STO, &raw);
    if (r == AEM10900_RES_OK)
        *voltage_v = 4.8f * ((float)raw / 256.0f);
    return r;
}

aem10900_res_e AEM10900_read_source_voltage(float * voltage_v)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    uint8_t raw;
    aem10900_res_e r = reg_read(AEM10900_REG_SRC, &raw);
    if (r == AEM10900_RES_OK)
    {
        uint8_t idx = raw & 0x3Fu;
        *voltage_v = (float)k_src_mv_lut[idx] / 1000.0f;
    }
    return r;
}

aem10900_res_e AEM10900_read_apm(float * result)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;

    uint8_t apm[3];
    aem10900_res_e r;
    r = reg_read(AEM10900_REG_APM0, &apm[0]);
    if (r != AEM10900_RES_OK) return r;
    r = reg_read(AEM10900_REG_APM1, &apm[1]);
    if (r != AEM10900_RES_OK) return r;
    r = reg_read(AEM10900_REG_APM2, &apm[2]);
    if (r != AEM10900_RES_OK) return r;

    uint8_t  shift = (apm[2] >> 4u) & 0x0Fu;
    uint32_t data  = ((uint32_t)(apm[2] & 0x0Fu) << 16u)
                   | ((uint32_t)apm[1] << 8u)
                   | (uint32_t)apm[0];

    *result = (float)(data << shift) * 0.10166f;
    return AEM10900_RES_OK;
}

aem10900_res_e AEM10900_read_reg(uint8_t reg, uint8_t * val)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    return reg_read(reg, val);
}

aem10900_res_e AEM10900_write_reg(uint8_t reg, uint8_t val)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    return reg_write(reg, val);
}

bool AEM10900_is_charging(void)
{
    uint8_t s = 0u;
    AEM10900_get_status(&s);
    return (s & AEM10900_STATUS_CHARGE) != 0u;
}
