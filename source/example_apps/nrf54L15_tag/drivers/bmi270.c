/* BMI270 6-axis IMU driver (SPI) — Bosch
 *
 * STUB implementation: SPI/CS plumbing, SPI-mode wake, soft-reset and the
 * CHIP_ID check are functional. The config-file upload is left as TODO.
 */

#include <stddef.h>

#include "bmi270.h"
#include "spi.h"
#include "gpio.h"

static bool     m_initialized = false;
static uint8_t  m_cs_id;

/* ── CS helpers ─────────────────────────────────────────────────────────────── */

static inline void cs_low(void)  { Gpio_outputWrite(m_cs_id, GPIO_LEVEL_LOW);  }
static inline void cs_high(void) { Gpio_outputWrite(m_cs_id, GPIO_LEVEL_HIGH); }

/* ── SPI register access ────────────────────────────────────────────────────── */

/* Read n bytes starting at reg. BMI270 returns one dummy byte after the
 * address byte on SPI reads, so we read (1 + n) and drop the first. */
static bmi270_res_e spi_read(uint8_t reg, uint8_t * buf, uint8_t n)
{
    uint8_t tx[1] = { (uint8_t)(reg | BMI270_SPI_READ) };
    uint8_t rx[1 + 16];

    if (n > 15u) return BMI270_RES_SPI_ERR;

    spi_xfer_t xfer = {
        .write_ptr  = tx,
        .write_size = 1,
        .read_ptr   = rx,
        .read_size  = (size_t)(n + 1u),  /* +1 dummy byte */
        .custom     = 0,
    };

    cs_low();
    spi_res_e r = SPI_transfer(&xfer, NULL);
    cs_high();
    if (r != SPI_RES_OK) return BMI270_RES_SPI_ERR;

    for (uint8_t i = 0; i < n; i++)
        buf[i] = rx[i + 1u];
    return BMI270_RES_OK;
}

static bmi270_res_e spi_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7Fu), val };

    spi_xfer_t xfer = {
        .write_ptr  = tx,
        .write_size = 2,
        .read_ptr   = NULL,
        .read_size  = 0,
        .custom     = 0,
    };

    cs_low();
    spi_res_e r = SPI_transfer(&xfer, NULL);
    cs_high();
    return (r == SPI_RES_OK) ? BMI270_RES_OK : BMI270_RES_SPI_ERR;
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

bmi270_res_e BMI270_init(const bmi270_cfg_t * cfg)
{
    m_cs_id = cfg->cs_gpio_id;

    /* CS: push-pull, idle HIGH (deasserted). */
    gpio_out_cfg_t cs_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_HIGH,
    };
    Gpio_outputSetCfg(m_cs_id, &cs_cfg);
    cs_high();

    spi_conf_t spi_conf = {
        .clock     = cfg->spi_clock_hz,
        .mode      = SPI_MODE_HIGH_SECOND,   /* BMI270: CPOL=1, CPHA=1 (mode 3) */
        .bit_order = SPI_ORDER_MSB,
    };
    spi_res_e sr = SPI_init(&spi_conf);
    if (sr != SPI_RES_OK && sr != SPI_RES_ALREADY_INITIALIZED)
        return BMI270_RES_SPI_ERR;

    /* Power-on defaults to I2C; a rising CS edge + one dummy read selects SPI. */
    uint8_t dummy;
    (void) spi_read(BMI270_REG_CHIP_ID, &dummy, 1u);

    /* Soft reset, then re-assert SPI mode with another dummy read. */
    bmi270_res_e r = spi_write(BMI270_REG_CMD, BMI270_CMD_SOFT_RESET);
    if (r != BMI270_RES_OK) return r;
    (void) spi_read(BMI270_REG_CHIP_ID, &dummy, 1u);

    uint8_t id = 0;
    r = spi_read(BMI270_REG_CHIP_ID, &id, 1u);
    if (r != BMI270_RES_OK) return r;
    if (id != BMI270_CHIP_ID_VALUE) return BMI270_RES_WRONG_ID;

    m_initialized = true;
    return BMI270_RES_OK;
}

bmi270_res_e BMI270_load_config(void)
{
    if (!m_initialized) return BMI270_RES_NOT_INITIALIZED;
    /* TODO: BMI270 requires its ~8 KB initialization blob to be streamed into
     *       INIT_DATA (with INIT_CTRL=0 during upload, =1 after), then verify
     *       INTERNAL_STATUS == 0x01, then enable acc+gyro via PWR_CTRL and set
     *       ACC_CONF / GYR_CONF. Drop in the Bosch bmi270_config_file[] here. */
    return BMI270_RES_NOT_IMPLEMENTED;
}

bool BMI270_data_ready(void)
{
    uint8_t s = 0;
    if (spi_read(BMI270_REG_STATUS, &s, 1u) != BMI270_RES_OK)
        return false;
    return (s & 0xC0u) != 0u;   /* drdy_acc | drdy_gyr */
}

bmi270_res_e BMI270_read(bmi270_sample_t * out)
{
    if (!m_initialized) return BMI270_RES_NOT_INITIALIZED;

    uint8_t raw[12];
    bmi270_res_e r = spi_read(BMI270_REG_ACC_DATA, raw, 12u);
    if (r != BMI270_RES_OK) return r;

    out->acc_x = (int16_t)((uint16_t)raw[0]  | ((uint16_t)raw[1]  << 8));
    out->acc_y = (int16_t)((uint16_t)raw[2]  | ((uint16_t)raw[3]  << 8));
    out->acc_z = (int16_t)((uint16_t)raw[4]  | ((uint16_t)raw[5]  << 8));
    out->gyr_x = (int16_t)((uint16_t)raw[6]  | ((uint16_t)raw[7]  << 8));
    out->gyr_y = (int16_t)((uint16_t)raw[8]  | ((uint16_t)raw[9]  << 8));
    out->gyr_z = (int16_t)((uint16_t)raw[10] | ((uint16_t)raw[11] << 8));
    return BMI270_RES_OK;
}

bmi270_res_e BMI270_read_reg(uint8_t reg, uint8_t * val)
{
    if (!m_initialized) return BMI270_RES_NOT_INITIALIZED;
    return spi_read(reg, val, 1u);
}

bmi270_res_e BMI270_write_reg(uint8_t reg, uint8_t val)
{
    if (!m_initialized) return BMI270_RES_NOT_INITIALIZED;
    return spi_write(reg, val);
}
