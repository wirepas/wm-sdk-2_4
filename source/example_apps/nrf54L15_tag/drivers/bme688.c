/* BME688 environmental sensor driver — Bosch SensorAPI wrapper (I2C)
 *
 * Platform HAL: Wirepas I2C HAL (i2c.h) + nRF54L15 NOP-loop delay.
 * The Bosch bme68x_api calls delay_us() only during soft-reset in
 * bme68x_init (~10 ms). All other timing is managed by the application.
 */

#include <string.h>
#include <stddef.h>

#include "bme688.h"
#include "bme68x_api/bme68x.h"
#include "i2c.h"
#include "mcu.h"     /* __NOP() */

/* ── Heater profile ─────────────────────────────────────────────────────────── */
/* 300 °C / 150 ms — standard for stable VOC gas resistance readings.
 * Lower temperature (e.g. 200 °C) shortens measurement time at reduced
 * sensitivity; higher temperature (400 °C) improves sensitivity at more power. */
#define HEATR_TEMP_DEG_C   300u
#define HEATR_DUR_MS       150u

#define I2C_CLOCK_HZ       400000u

/* Max write payload: reg_addr (1) + data (up to 16 bytes config).
 * Bosch API never writes more than ~10 bytes in one call. */
#define WRITE_BUF_MAX      32u

static bool              m_initialized = false;
static uint8_t           m_i2c_addr;
static struct bme68x_dev m_dev;
static struct bme68x_conf       m_conf;
static struct bme68x_heatr_conf m_heatr;

/* ── Bosch HAL callbacks ─────────────────────────────────────────────────────── */

static BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr,
                                              uint8_t * reg_data,
                                              uint32_t  length,
                                              void    * intf_ptr)
{
    uint8_t addr = *(const uint8_t *)intf_ptr;
    i2c_xfer_t xfer = {
        .address    = addr,
        .write_ptr  = &reg_addr,
        .write_size = 1u,
        .read_ptr   = reg_data,
        .read_size  = length,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? BME68X_OK : BME68X_E_COM_FAIL;
}

static BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t        reg_addr,
                                               const uint8_t * reg_data,
                                               uint32_t        length,
                                               void          * intf_ptr)
{
    if (length + 1u > WRITE_BUF_MAX) return BME68X_E_COM_FAIL;

    uint8_t addr = *(const uint8_t *)intf_ptr;
    uint8_t buf[WRITE_BUF_MAX];
    buf[0] = reg_addr;
    memcpy(&buf[1], reg_data, length);

    i2c_xfer_t xfer = {
        .address    = addr,
        .write_ptr  = buf,
        .write_size = length + 1u,
        .read_ptr   = NULL,
        .read_size  = 0u,
    };
    return (I2C_transfer(&xfer, NULL) == I2C_RES_OK)
           ? BME68X_OK : BME68X_E_COM_FAIL;
}

/* Called only during bme68x_init soft-reset (~10 ms).
 * Calibrated for nRF54L15 @ 64 MHz: ~5 cycles / iteration ≈ 78 ns.
 * period * 13 ≈ period µs. Accuracy not critical for a one-shot reset wait. */
static void bme68x_delay_us(uint32_t period, void * intf_ptr)
{
    (void)intf_ptr;
    volatile uint32_t n = period * 13u;
    while (n--) { __NOP(); }
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

bme688_res_e BME688_init(const bme688_cfg_t * cfg)
{
    i2c_conf_t i2c_conf = {
        .clock  = I2C_CLOCK_HZ,
        .pullup = cfg->pullup,
    };
    i2c_res_e ir = I2C_init(&i2c_conf);
    if (ir != I2C_RES_OK && ir != I2C_RES_ALREADY_INITIALIZED)
        return BME688_RES_I2C_ERR;

    m_i2c_addr = cfg->i2c_addr;

    m_dev.intf     = BME68X_I2C_INTF;
    m_dev.intf_ptr = &m_i2c_addr;
    m_dev.read     = bme68x_i2c_read;
    m_dev.write    = bme68x_i2c_write;
    m_dev.delay_us = bme68x_delay_us;
    m_dev.amb_temp = 25;   /* initial ambient temperature estimate in °C */

    int8_t rslt = bme68x_init(&m_dev);
    if (rslt == BME68X_E_DEV_NOT_FOUND) return BME688_RES_WRONG_ID;
    if (rslt != BME68X_OK)              return BME688_RES_API_ERR;

    /* Oversampling: T×2, P×1, H×1, no IIR filter (forced mode, single-shot) */
    m_conf.os_temp  = BME68X_OS_2X;
    m_conf.os_pres  = BME68X_OS_1X;
    m_conf.os_hum   = BME68X_OS_1X;
    m_conf.filter   = BME68X_FILTER_OFF;
    m_conf.odr      = BME68X_ODR_NONE;
    rslt = bme68x_set_conf(&m_conf, &m_dev);
    if (rslt != BME68X_OK) return BME688_RES_API_ERR;

    /* Gas heater: 300 °C / 150 ms — BME688 (variant HIGH) required for gas.
     * BME680 (variant LOW) uses ENABLE_GAS_MEAS_L. */
    m_heatr.enable    = BME68X_ENABLE;
    m_heatr.heatr_temp = HEATR_TEMP_DEG_C;
    m_heatr.heatr_dur  = HEATR_DUR_MS;
    rslt = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &m_heatr, &m_dev);
    if (rslt != BME68X_OK) return BME688_RES_API_ERR;

    m_initialized = true;
    return BME688_RES_OK;
}

bme688_res_e BME688_trigger(void)
{
    if (!m_initialized) return BME688_RES_NOT_INITIALIZED;

    int8_t rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &m_dev);
    return (rslt == BME68X_OK) ? BME688_RES_OK : BME688_RES_API_ERR;
}

uint32_t BME688_meas_duration_us(void)
{
    /* T/P/H sampling time (µs) + heater duration (µs).
     * Add 5 % margin for clock tolerances. */
    uint32_t dur = bme68x_get_meas_dur(BME68X_FORCED_MODE, &m_conf, &m_dev);
    dur += (uint32_t)HEATR_DUR_MS * 1000u;
    return dur + dur / 20u;   /* +5 % margin */
}

bme688_res_e BME688_read(bme688_result_t * out)
{
    if (!m_initialized) return BME688_RES_NOT_INITIALIZED;

    struct bme68x_data data;
    uint8_t n_fields = 0u;

    int8_t rslt = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &m_dev);
    if (rslt != BME68X_OK)  return BME688_RES_API_ERR;
    if (n_fields == 0u)     return BME688_RES_NO_DATA;

    out->temperature    = data.temperature;
    out->pressure       = data.pressure;
    out->humidity       = data.humidity;
    out->gas_resistance = data.gas_resistance;
    out->gas_valid      = (data.status & BME68X_GASM_VALID_MSK) != 0u;
    out->heat_stable    = (data.status & BME68X_HEAT_STAB_MSK)  != 0u;

    /* Keep ambient temperature estimate up to date for heater compensation. */
    m_dev.amb_temp = (int8_t)data.temperature;

    return BME688_RES_OK;
}
