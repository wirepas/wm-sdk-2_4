/* AEM10900 PMIC driver — e-peas energy harvesting IC (I2C) */

#include <stddef.h>

#include "aem10900.h"
#include "i2c.h"

#define AEM10900_I2C_ADDR   0x41u     /* confirmed by I2C bus scan (not 0x2D) */
#define I2C_CLOCK_HZ        100000u   /* 100 kHz standard mode */
#define SYNCBUSY_RETRIES    100u

/* APM Power-Meter scale: pwr_uW = (POWER << OFFSET) * AEM10900_APM_SCALE_UW.
 * Calibrated empirically from a measured operating point:
 *   P = Vsrc 1.43 V x Isrc 200 uA = 286 uW, raw (POWER<<OFFSET) = 17433
 *   scale = 286 / 17433 = 0.0164.
 * Refine with the e-peas alpha / L_DCDC if an exact figure is needed. */
#define AEM10900_APM_SCALE_UW   0.0164f

static bool m_initialized = false;

/* Note: VSRC (reg 0x13) is a linear 8-bit code, 4.8 V full-scale — same encoding
 * as the storage voltage (verified on hardware). The earlier lookup table from
 * "datasheet Table 5" was incorrect and has been removed. */

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
        .pullup = true,     /* sensorv26 has no external I2C pull-ups */
    };
    /* Force our clock to take effect even if another driver initialised the
     * shared bus first (I2C_init would otherwise return ALREADY_INITIALIZED
     * and keep the previous clock). */
    (void)I2C_close();
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

/* SRC register code → source voltage, per e-peas CFG-AEMx090x.xlsx (cell G32).
 * The reading is piecewise (the code lands in one of three ratio-dependent
 * bands); codes in the unused gaps are clamped up to the nearest valid band. */
static float src_code_to_v(uint8_t raw)
{
    int g = raw;
    if (g <= 57)
    {
        if (g <= 6)  return 0.112f;
        if (g <= 18) return 0.09f + (float)(2 * g -  9) * 0.0075f;
        return             0.30f + (float)(2 * g - 37) * 0.015f;
    }
    if (g <= 121)                      /* 58–103 (gap) clamped into this band */
    {
        if (g < 104) g = 104;
        return (0.30f + (float)(2 * g - 165) * 0.015f) / 0.67f;
    }
    if (g < 159) g = 159;              /* 122–158 (gap) clamped */
    if (g > 166) g = 166;             /* above full-scale clamped */
    return (0.30f + (float)(2 * g - 293) * 0.015f) / 0.33f;
}

aem10900_res_e AEM10900_read_source_voltage(float * voltage_v)
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;
    uint8_t raw;
    aem10900_res_e r = reg_read(AEM10900_REG_SRC, &raw);
    if (r == AEM10900_RES_OK)
        *voltage_v = src_code_to_v(raw);
    return r;
}

aem10900_res_e AEM10900_read_apm(float * result, uint8_t raw_out[3])
{
    if (!m_initialized) return AEM10900_RES_NOT_INITIALIZED;

    /* Read APM0..APM2 in ONE auto-increment burst so the 22-bit field is a
     * coherent snapshot. Three separate reads can straddle an APM update
     * (~128/256 ms window) and combine bytes from different samples, which
     * corrupts the OFFSET exponent and makes the power swing wildly. */
    uint8_t reg = AEM10900_REG_APM0;
    uint8_t apm[3];
    i2c_xfer_t xfer = {
        .address    = AEM10900_I2C_ADDR,
        .write_ptr  = &reg,
        .write_size = 1u,
        .read_ptr   = apm,
        .read_size  = 3u,
    };
    if (I2C_transfer(&xfer, NULL) != I2C_RES_OK)
        return AEM10900_RES_I2C_ERR;

    if (raw_out != NULL)
    {
        raw_out[0] = apm[0];
        raw_out[1] = apm[1];
        raw_out[2] = apm[2];
    }

    /* Power Meter mode (datasheet §9.13, Table 30):
     *   OFFSET = APM2[6:3]   (bit 7 unused)
     *   POWER  = APM2[2:0] : APM1[7:0] : APM0[7:0]   (19-bit)
     *   E_APM  = (POWER << OFFSET) * alpha / L_DCDC.
     * The 0.10166 factor folds alpha / L_DCDC / window — application-specific
     * (e-peas must supply alpha); only the bit extraction is from the datasheet. */
    uint8_t  offset = (apm[2] >> 3u) & 0x0Fu;
    uint32_t power  = ((uint32_t)(apm[2] & 0x07u) << 16u)
                    | ((uint32_t)apm[1] << 8u)
                    | (uint32_t)apm[0];

    *result = (float)(power << offset) * AEM10900_APM_SCALE_UW;
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
