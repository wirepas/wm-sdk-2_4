/* Software (bit-bang) I2C for canopee_v27.
 *
 * WHY: on nRF54L15 the low-power domain has a single serial peripheral,
 * SERIAL30 (UARTE30 == SPIM30 == TWIM30, same instance and IRQ 260). canopee_v27
 * puts the RS485 UART on P0 (→ UARTE30) AND the I2C on P0.03/P0.04 (→ TWIM30):
 * both cannot use SERIAL30 at once. RS485 keeps the hardware UARTE30; the I2C
 * (LIS2DW + MAX17261) is bit-banged on GPIO here.
 *
 * Provides the same API as mcu/hal_api/i2c.h (I2C_init / I2C_transfer / ...),
 * so the LIS2DW and MAX17261 drivers use it unchanged. Build with HAL_I2C=no so
 * the hardware HAL i2c.c is not linked (no duplicate symbols).
 *
 * Open-drain emulation: OUT latch stays 0; a line is released high by switching
 * the pad to input (internal pull-up), and driven low by switching it to output.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "nrf.h"
#include "nrf_gpio.h"
#include "board.h"
#include "i2c.h"

/* Port-aware absolute pin numbers (P0.03 = 3, P0.04 = 4). The nrf_gpio HAL
 * decodes the port itself, so raw NRF_P0->PIN_CNF[] is NOT used — that never
 * connected the input buffer on nRF54L (IN read 0 for every line/ACK). */
#define SCL_PIN   (BOARD_I2C_SCL_PIN)   /* P0.03 */
#define SDA_PIN   (BOARD_I2C_SDA_PIN)   /* P0.04 */

/* Half-bit delay (NOP loop). ~60 iterations → tens of kHz; I2C is clock-
 * tolerant and the LIS2DW/MAX17261 are slow, so a relaxed rate is fine. */
#define I2C_HALF_DLY   120u
/* Bounded stretch wait. Kept small so a stuck line can never block the
 * cooperative scheduler long enough to trip the Wirepas LL watchdog. */
#define SCL_STRETCH_MAX 200u

static bool m_init = false;

static inline void dly(void)
{
    for (volatile uint32_t i = 0; i < I2C_HALF_DLY; i++) { __asm volatile("nop"); }
}

/* S0D1 open-drain: pin_set() lets the pull-up raise the line (high-side
 * disconnected), pin_clear() drives it low. Configured once in I2C_init. */
static inline void scl_release(void) { nrf_gpio_pin_set(SCL_PIN); }
static inline void scl_drive_low(void){ nrf_gpio_pin_clear(SCL_PIN); }
static inline void sda_release(void) { nrf_gpio_pin_set(SDA_PIN); }
static inline void sda_drive_low(void){ nrf_gpio_pin_clear(SDA_PIN); }
static inline uint32_t sda_read(void) { return nrf_gpio_pin_read(SDA_PIN); }
static inline uint32_t scl_read(void) { return nrf_gpio_pin_read(SCL_PIN); }

/* Release SCL and wait (bounded) for it to actually rise (clock stretching). */
static void scl_high_wait(void)
{
    scl_release();
    for (uint32_t g = 0; g < SCL_STRETCH_MAX && scl_read() == 0u; g++) { }
    dly();
}

static void i2c_start(void)
{
    sda_release(); dly();
    scl_high_wait();
    sda_drive_low(); dly();      /* SDA falls while SCL high = START */
    scl_drive_low(); dly();
}

static void i2c_stop(void)
{
    sda_drive_low(); dly();
    scl_high_wait();
    sda_release(); dly();        /* SDA rises while SCL high = STOP */
}

static bool i2c_write_byte(uint8_t b)
{
    for (uint8_t i = 0; i < 8u; i++)
    {
        if (b & 0x80u) sda_release(); else sda_drive_low();
        dly();
        scl_high_wait();
        scl_drive_low();
        dly();
        b = (uint8_t)(b << 1);
    }
    /* 9th clock: read ACK (0 = ACK) */
    sda_release();
    dly();
    scl_high_wait();
    uint32_t ack = sda_read();
    scl_drive_low();
    dly();
    return (ack == 0u);
}

static uint8_t i2c_read_byte(bool send_ack)
{
    uint8_t b = 0;
    sda_release();               /* let the slave drive SDA */
    for (uint8_t i = 0; i < 8u; i++)
    {
        dly();
        scl_high_wait();
        b = (uint8_t)((b << 1) | (sda_read() & 1u));
        scl_drive_low();
        dly();
    }
    /* master ACK/NAK */
    if (send_ack) sda_drive_low(); else sda_release();
    dly();
    scl_high_wait();
    scl_drive_low();
    sda_release();
    dly();
    return b;
}

i2c_res_e I2C_init(i2c_conf_t * conf_p)
{
    (void)conf_p;   /* internal pull-ups always used; rate is fixed by dly() */
    if (m_init) return I2C_RES_ALREADY_INITIALIZED;

    /* Open-drain (S0D1) output with input buffer connected + internal pull-up.
     * Start released (OUT=1) so both lines idle high. */
    nrf_gpio_pin_set(SCL_PIN);
    nrf_gpio_pin_set(SDA_PIN);
    nrf_gpio_cfg(SCL_PIN, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_cfg(SDA_PIN, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);

    m_init = true;
    return I2C_RES_OK;
}

i2c_res_e I2C_close(void) { m_init = false; return I2C_RES_OK; }
i2c_res_e I2C_status(void) { return I2C_RES_OK; }

i2c_res_e I2C_transfer(i2c_xfer_t * xfer_p, i2c_on_transfer_done_cb_f cb)
{
    if (!m_init)          return I2C_RES_NOT_INITIALIZED;
    if (xfer_p == NULL)   return I2C_RES_INVALID_XFER;

    i2c_res_e res = I2C_RES_OK;
    bool started = false;

    if (xfer_p->write_size > 0u && xfer_p->write_ptr != NULL)
    {
        i2c_start(); started = true;
        if (!i2c_write_byte((uint8_t)(xfer_p->address << 1)))       { res = I2C_RES_ANACK; goto done; }
        for (uint32_t i = 0; i < xfer_p->write_size; i++)
            if (!i2c_write_byte(xfer_p->write_ptr[i]))              { res = I2C_RES_DNACK; goto done; }
    }

    if (xfer_p->read_size > 0u && xfer_p->read_ptr != NULL)
    {
        i2c_start(); started = true;   /* repeated-start (or plain start on pure read) */
        if (!i2c_write_byte((uint8_t)((xfer_p->address << 1) | 1u))) { res = I2C_RES_ANACK; goto done; }
        for (uint32_t i = 0; i < xfer_p->read_size; i++)
            xfer_p->read_ptr[i] = i2c_read_byte(i < (xfer_p->read_size - 1u));  /* ACK all but last */
    }

done:
    if (started) i2c_stop();
    if (cb != NULL) cb(res, xfer_p);
    return res;
}
