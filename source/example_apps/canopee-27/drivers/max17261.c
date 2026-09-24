/* MAX17261 ModelGauge m5 fuel-gauge driver — see max17261.h */

#include <stddef.h>
#include "max17261.h"
#include "i2c.h"

#define I2C_CLOCK_HZ        100000u

/* Register map (16-bit each, little-endian) */
#define MAX17261_REG_STATUS   0x00u
#define MAX17261_REG_TALRTTH  0x02u   /* temp alert thresholds [15:8]=max [7:0]=min */
#define MAX17261_REG_REPCAP   0x05u
#define MAX17261_REG_REPSOC   0x06u
#define MAX17261_REG_AGE      0x07u   /* FullCapRep/DesignCap ratio (% , 1/256 LSB) */
#define MAX17261_REG_TEMP     0x08u
#define MAX17261_REG_VCELL    0x09u
#define MAX17261_REG_CURRENT  0x0Au
#define MAX17261_REG_AVGCURR  0x0Bu   /* AvgCurrent (signed, same LSB as Current)   */
#define MAX17261_REG_FULLCAP  0x10u   /* FullCapRep — learned full capacity         */
#define MAX17261_REG_TTE      0x11u   /* TimeToEmpty (5.625 s LSB)                   */
#define MAX17261_REG_CYCLES   0x17u   /* equivalent full cycles (1% LSB)            */
#define MAX17261_REG_DESIGNCAP 0x18u  /* DesignCap — nameplate capacity             */
#define MAX17261_REG_TTF      0x20u   /* TimeToFull  (5.625 s LSB)                   */
#define MAX17261_REG_TIMERH   0xBEu   /* powered lifetime (3.2 h LSB)               */
#define MAX17261_REG_FULLCAPNOM 0x23u /* nominal full capacity (algorithm seed)     */
#define MAX17261_REG_CONFIG   0x1Du   /* control: thermistor / temperature src */
#define MAX17261_REG_DEVNAME  0x21u   /* used only as a presence probe */
#define MAX17261_REG_TGAIN    0x2Cu   /* thermistor curve gain                 */
#define MAX17261_REG_TOFF     0x2Du   /* thermistor curve offset               */
#define MAX17261_REG_TCURVE   0xB9u   /* thermistor curve compensation         */

/* Config (0x1D) bits (datasheet §8.5). Only the temperature-source bits below
 * are touched (read-modify-write) so SHDN/alert config is preserved. */
#define MAX17261_CFG_AEN      (1u << 2)  /* enable alerts on the ALRT pin         */
#define MAX17261_CFG_FTHRM    (1u << 3)  /* force thermistor bias switch on      */
#define MAX17261_CFG_ETHRM    (1u << 4)  /* enable ext thermistor (0 = die temp) */
#define MAX17261_CFG_TEX      (1u << 8)  /* temp from host writes (0 = IC meas.)  */
#define MAX17261_CFG_TEN      (1u << 9)  /* enable temperature channel           */
#define MAX17261_CFG_TSEL     (1u << 15) /* select thermistor as the temp source */

/* Status (0x00) bits. */
#define MAX17261_ST_POR       (1u << 1)  /* power-on reset — set until cleared     */
#define MAX17261_ST_TMN       (1u << 9)  /* temperature below TAlrtTh min         */
#define MAX17261_ST_TMX       (1u << 13) /* temperature above TAlrtTh max         */

/* Temperature alert window: min in low byte, max in high byte (signed °C). */
#define MAX17261_TALRT_MIN_C  0    /* 0x00 */
#define MAX17261_TALRT_MAX_C  41   /* 0x29 — charge cutoff high threshold */

/* Thermistor curve values for a 10 kΩ β=3435 NTC (Maxim reference set). */
/* Stock Maxim 10 kΩ / β=3435 thermistor curve. */
#define MAX17261_TGAIN_10K    0xEE56u
#define MAX17261_TOFF_10K     0x1DA4u
#define MAX17261_TCURVE_10K   0x0025u

/* Fixed-point LSB values (datasheet §8.1) */
#define MAX17261_VCELL_UV       78.125f   /* VCell LSB = 78.125 µV                */
#define MAX17261_SOC_LSB      (1.0f/256)  /* RepSOC LSB = 1/256 %                 */
#define MAX17261_TEMP_LSB     (1.0f/256)  /* Temp   LSB = 1/256 °C (signed)       */
#define MAX17261_CURRENT_UV     1.5625f   /* Current LSB = 1.5625 µV / Rsense     */
#define MAX17261_CAP_UVH        5.0f      /* Capacity LSB = 5.0 µVh / Rsense      */
#define MAX17261_PCT_LSB      (1.0f/256)  /* Age LSB = 1/256 %                    */
#define MAX17261_CYCLES_LSB     0.01f     /* Cycles LSB = 1% → 0.01 full cycle    */
#define MAX17261_TIME_LSB_S     5.625f    /* TTE/TTF LSB = 5.625 s                */
#define MAX17261_TIMERH_LSB_H   3.2f      /* TimerH LSB = 3.2 h                   */

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

static max17261_res_e reg_write16(uint8_t reg, uint16_t val)
{
    uint8_t tx[3] = { reg, (uint8_t)(val & 0xFFu), (uint8_t)(val >> 8) };
    i2c_xfer_t xfer = {
        .address    = MAX17261_I2C_ADDR,
        .write_ptr  = tx,
        .write_size = 3u,
        .read_ptr   = NULL,
        .read_size  = 0u,
    };
    if (I2C_transfer(&xfer, NULL) != I2C_RES_OK)
        return MAX17261_RES_I2C_ERR;
    return MAX17261_RES_OK;
}

max17261_res_e MAX17261_init(uint16_t rsense_mohm, uint16_t design_cap_mah, int pullup)
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

    /* Temperature source = external NTC via the MAX17261's own INTERNAL bias,
     * kept on continuously. This config gives a coherent NTC reading, which the
     * app uses to gate the LTC4121 charger via CHG_EN (0..40 °C window).
     *   1) TGAIN/TOFF/TCURVE  — 10 kΩ β=3435 NTC curve.
     *   2) Config (RMW, keep SHDN/alerts):
     *        TSEL  (b15) select thermistor as the temperature source
     *        TEN   (b9)  enable the temperature channel
     *        ETHRM (b4)  enable the internal thermistor bias
     *        FTHRM (b3)  keep the bias switch on so AIN is always valid
     *        Tex   (b8)  cleared → the IC measures (not host-supplied)
     * Best-effort: a failure here does not abort init (the gauge still works). */
    (void)reg_write16(MAX17261_REG_TGAIN,  MAX17261_TGAIN_10K);
    (void)reg_write16(MAX17261_REG_TOFF,   MAX17261_TOFF_10K);
    (void)reg_write16(MAX17261_REG_TCURVE, MAX17261_TCURVE_10K);

    /* Temperature alert window [0, 40] °C on the ALRT pin (hardware charge guard,
     * independent of the host read cadence). Threshold reg + Aen in Config. */
    (void)reg_write16(MAX17261_REG_TALRTTH,
                      (uint16_t)(((uint8_t)MAX17261_TALRT_MAX_C << 8)
                                 | (uint8_t)MAX17261_TALRT_MIN_C));

    uint16_t cfg;
    if (reg_read16(MAX17261_REG_CONFIG, &cfg) == MAX17261_RES_OK)
    {
        cfg = (uint16_t)((cfg | MAX17261_CFG_TSEL | MAX17261_CFG_TEN
                              | MAX17261_CFG_ETHRM | MAX17261_CFG_FTHRM
                              | MAX17261_CFG_AEN)
                         & ~MAX17261_CFG_TEX);
        (void)reg_write16(MAX17261_REG_CONFIG, cfg);
    }

    /* One-time capacity seed on a fresh power-up (Status.POR set): program
     * DesignCap and seed the learned full-capacity registers so Age (= FullCap/
     * DesignCap), Cycles and capacity read meaningfully for THIS pack. Non-
     * blocking (no ModelCFG reload/poll). POR is cleared so a warm reboot keeps
     * the learned state; a real power cycle re-seeds with the same values. */
    uint16_t st0;
    if (design_cap_mah != 0u
        && reg_read16(MAX17261_REG_STATUS, &st0) == MAX17261_RES_OK
        && (st0 & MAX17261_ST_POR))
    {
        /* Capacity LSB = 5.0 µVh / Rsense → raw = mAh * Rsense_mΩ / 5. */
        uint16_t cap_raw = (uint16_t)(((uint32_t)design_cap_mah * m_rsense_mohm) / 5u);
        (void)reg_write16(MAX17261_REG_DESIGNCAP,  cap_raw);
        (void)reg_write16(MAX17261_REG_FULLCAP,    cap_raw);
        (void)reg_write16(MAX17261_REG_FULLCAPNOM, cap_raw);
        st0 &= (uint16_t)~MAX17261_ST_POR;
        (void)reg_write16(MAX17261_REG_STATUS, st0);
    }

    m_initialized = true;
    return MAX17261_RES_OK;
}

void MAX17261_clear_temp_alert(void)
{
    /* Clear the sticky temp min/max status bits so the ALRT pin can re-arm. */
    uint16_t st;
    if (reg_read16(MAX17261_REG_STATUS, &st) != MAX17261_RES_OK) return;
    st &= (uint16_t)~(MAX17261_ST_TMN | MAX17261_ST_TMX);
    (void)reg_write16(MAX17261_REG_STATUS, st);
}

max17261_res_e MAX17261_read_reg(uint8_t reg, uint16_t * val)
{
    if (val == NULL) return MAX17261_RES_I2C_ERR;
    return reg_read16(reg, val);
}

max17261_res_e MAX17261_write_reg(uint8_t reg, uint16_t val)
{
    return reg_write16(reg, val);
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

max17261_res_e MAX17261_read_health(max17261_health_t * out)
{
    if (!m_initialized) return MAX17261_RES_NOT_INITIALIZED;
    if (out == NULL)    return MAX17261_RES_I2C_ERR;

    uint16_t avgcur, repcap, fullcap, dsgncap, age, cycles, tte, ttf, timerh;
    max17261_res_e r;

    if ((r = reg_read16(MAX17261_REG_AVGCURR,   &avgcur))  != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_REPCAP,    &repcap))  != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_FULLCAP,   &fullcap)) != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_DESIGNCAP, &dsgncap)) != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_AGE,       &age))     != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_CYCLES,    &cycles))  != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_TTE,       &tte))     != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_TTF,       &ttf))     != MAX17261_RES_OK) return r;
    if ((r = reg_read16(MAX17261_REG_TIMERH,    &timerh))  != MAX17261_RES_OK) return r;

    const float cap_mah = MAX17261_CAP_UVH / (float)m_rsense_mohm;   /* mAh per LSB */

    out->avg_current_ma = (int16_t)((float)(int16_t)avgcur
                                    * MAX17261_CURRENT_UV / (float)m_rsense_mohm);
    out->rep_cap_mah    = (float)repcap  * cap_mah;
    out->full_cap_mah   = (float)fullcap * cap_mah;
    out->design_cap_mah = (float)dsgncap * cap_mah;
    out->age_pct        = (float)age     * MAX17261_PCT_LSB;
    out->cycles         = (float)cycles  * MAX17261_CYCLES_LSB;
    out->tte_s          = (uint32_t)((float)tte * MAX17261_TIME_LSB_S);
    out->ttf_s          = (uint32_t)((float)ttf * MAX17261_TIME_LSB_S);
    out->timer_h        = (float)timerh  * MAX17261_TIMERH_LSB_H;
    return MAX17261_RES_OK;
}
