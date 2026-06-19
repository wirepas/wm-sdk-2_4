/* ADS1220 24-bit ADC driver (SPI Mode 1: CPOL=0, CPHA=1)
 *
 * SPI is initialised once in ADS1220_init() and stays open for all subsequent
 * transfers.  The previous pattern of SPI_close()+SPI_init() inside each
 * transfer reset SCK/MOSI/MISO to GPIO-default (input) while CS was held low,
 * causing glitches that confused the chip and made the clock invisible on a
 * scope (8 clocks at 4 MHz = 2 µs, hidden in the reconfiguration overhead).
 *
 * Wirepas SPI mode naming:
 *   SPI_MODE_HIGH_SECOND = first edge HIGH (clock idles LOW = CPOL=0),
 *                          data latched on second edge (CPHA=1) = SPI Mode 1.
 * The old driver used SPI_MODE_LOW_SECOND = Mode 3 (CPOL=1, CPHA=1) — wrong.
 */

#include <stddef.h>

#include "ads1220.h"
#include "spi.h"
#include "gpio.h"

/* 1 MHz during bringup: each byte = 8 µs, easy to see on a scope.
 * Restore to 4000000u once the hardware is verified. */
#define ADS1220_SPI_CLOCK_HZ    1000000u

static bool    m_initialized = false;
static uint8_t m_cs_id;
static uint8_t m_drdy_id;
static uint8_t m_reg0_cache = 0;   /* cached REG0 to allow MUX-only updates */

static const spi_conf_t m_spi_conf = {
    .clock     = ADS1220_SPI_CLOCK_HZ,
    .mode      = SPI_MODE_HIGH_SECOND,  /* CPOL=0, CPHA=1 = SPI Mode 1 */
    .bit_order = SPI_ORDER_MSB,
};

/* ── CS helpers ─────────────────────────────────────────────────────────────── */

static void cs_assert(void)   { Gpio_outputWrite(m_cs_id, GPIO_LEVEL_LOW);  }
static void cs_deassert(void) { Gpio_outputWrite(m_cs_id, GPIO_LEVEL_HIGH); }

/* ── SPI primitives (SPI must already be initialised) ────────────────────────── */

static ads1220_res_e spi_write(const uint8_t * data, size_t len)
{
    spi_xfer_t x = {
        .write_ptr  = (uint8_t *)data,
        .write_size = len,
        .read_ptr   = NULL,
        .read_size  = 0,
    };
    return (SPI_transfer(&x, NULL) == SPI_RES_OK) ? ADS1220_RES_OK
                                                   : ADS1220_RES_SPI_ERR;
}

static ads1220_res_e spi_read(uint8_t * data, size_t len)
{
    spi_xfer_t x = {
        .write_ptr  = NULL,
        .write_size = 0,
        .read_ptr   = data,
        .read_size  = len,
    };
    return (SPI_transfer(&x, NULL) == SPI_RES_OK) ? ADS1220_RES_OK
                                                   : ADS1220_RES_SPI_ERR;
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

ads1220_res_e ADS1220_init(uint8_t cs_gpio_id,
                            uint8_t drdy_gpio_id,
                            const ads1220_regs_t * cfg)
{
    m_cs_id   = cs_gpio_id;
    m_drdy_id = drdy_gpio_id;

    /* CS: push-pull, idle HIGH */
    gpio_out_cfg_t cs_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_HIGH,
    };
    Gpio_outputSetCfg(m_cs_id, &cs_cfg);

    /* DRDY: input with internal pull-up as fallback.
     * DRDY is active-low; idle HIGH.  Without pull-up a disconnected pin
     * floats LOW and looks like "data ready" before any conversion. */
    gpio_in_cfg_t drdy_cfg = {
        .event_cb    = NULL,
        .event_cfg   = GPIO_IN_EVENT_NONE,
        .in_mode_cfg = GPIO_IN_PULL_UP,
    };
    Gpio_inputSetCfg(m_drdy_id, &drdy_cfg);

    /* Init SPI once — stays open for all transfers. SPI_close() first in case
     * the HAL was left initialised from a previous call (e.g. warm reset). */
    SPI_close();
    if (SPI_init((spi_conf_t *)&m_spi_conf) != SPI_RES_OK)
        return ADS1220_RES_SPI_ERR;

    /* RESET command: bring the chip to a known state */
    uint8_t cmd = ADS1220_CMD_RESET;
    cs_assert();
    ads1220_res_e r = spi_write(&cmd, 1u);
    cs_deassert();
    if (r != ADS1220_RES_OK) return r;

    /* Write configuration registers */
    m_reg0_cache = cfg->reg[0];
    r = ADS1220_write_regs(cfg);
    if (r != ADS1220_RES_OK) return r;

    m_initialized = true;
    return ADS1220_RES_OK;
}

ads1220_res_e ADS1220_power_down(void)
{
    if (!m_initialized) return ADS1220_RES_NOT_INITIALIZED;
    uint8_t cmd = ADS1220_CMD_POWERDOWN;
    cs_assert();
    ads1220_res_e r = spi_write(&cmd, 1u);
    cs_deassert();
    return r;
}

ads1220_res_e ADS1220_start(void)
{
    if (!m_initialized) return ADS1220_RES_NOT_INITIALIZED;
    uint8_t cmd = ADS1220_CMD_START;
    cs_assert();
    ads1220_res_e r = spi_write(&cmd, 1u);
    cs_deassert();
    return r;
}

ads1220_res_e ADS1220_read_result(int32_t * result, uint32_t timeout_ms)
{
    if (!m_initialized) return ADS1220_RES_NOT_INITIALIZED;

    /* Poll DRDY (active-low) */
    uint32_t ticks = timeout_ms * 1000u;
    gpio_level_e level;
    do {
        Gpio_inputRead(m_drdy_id, &level);
        if (ticks-- == 0u) return ADS1220_RES_NOT_READY;
    } while (level != GPIO_LEVEL_LOW);

    /* RDATA: send command byte, then read 3 data bytes in the same CS frame */
    uint8_t cmd  = ADS1220_CMD_RDATA;
    uint8_t raw[3] = {0, 0, 0};
    ads1220_res_e r;

    cs_assert();
    r = spi_write(&cmd, 1u);
    if (r == ADS1220_RES_OK)
        r = spi_read(raw, 3u);
    cs_deassert();

    if (r != ADS1220_RES_OK) return r;

    int32_t val = ((int32_t)raw[0] << 16)
                | ((int32_t)raw[1] <<  8)
                |  (int32_t)raw[2];
    if (val & 0x800000L)
        val |= (int32_t)0xFF000000L;

    *result = val;
    return ADS1220_RES_OK;
}

ads1220_res_e ADS1220_write_regs(const ads1220_regs_t * regs)
{
    uint8_t buf[5];
    buf[0] = ADS1220_CMD_WREG(0u, 4u);
    buf[1] = regs->reg[0];
    buf[2] = regs->reg[1];
    buf[3] = regs->reg[2];
    buf[4] = regs->reg[3];

    cs_assert();
    ads1220_res_e r = spi_write(buf, 5u);
    cs_deassert();
    return r;
}

ads1220_res_e ADS1220_read_regs(ads1220_regs_t * regs)
{
    uint8_t cmd = ADS1220_CMD_RREG(0u, 4u);
    ads1220_res_e r;

    cs_assert();
    r = spi_write(&cmd, 1u);
    if (r == ADS1220_RES_OK)
        r = spi_read(regs->reg, 4u);
    cs_deassert();
    return r;
}

ads1220_res_e ADS1220_set_mux(uint8_t mux)
{
    if (!m_initialized) return ADS1220_RES_NOT_INITIALIZED;
    /* mux = ADS1220_MUX_xxx (already occupies bits [7:4] of REG0) */
    m_reg0_cache = (m_reg0_cache & 0x0Fu) | (mux & 0xF0u);
    uint8_t buf[2] = { ADS1220_CMD_WREG(0u, 1u), m_reg0_cache };
    cs_assert();
    ads1220_res_e r = spi_write(buf, 2u);
    cs_deassert();
    return r;
}

ads1220_res_e ADS1220_read_single(int32_t * result, uint32_t timeout_ms)
{
    ads1220_res_e r = ADS1220_start();
    if (r != ADS1220_RES_OK) return r;
    return ADS1220_read_result(result, timeout_ms);
}
