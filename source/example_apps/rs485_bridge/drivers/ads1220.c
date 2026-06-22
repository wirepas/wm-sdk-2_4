/* ADS1220 24-bit ADC driver — raw SPIM00 register access on nRF54L15
 *
 * On nRF54L15 there are two GPIO / peripheral domains:
 *   LP domain  (0x4004x/0x4005x/0x4010x): P0, P2, SPIM00, SPIM30/UARTE30
 *   Peripheral domain (0x400Cx/0x400Dx):  P1, SPIM20-22, UARTE20
 *
 * The ADS1220 SPI signals are routed to P2 (LP domain) on this board.
 * SPIM22 (peripheral domain) cannot drive P2 pins via PSEL — it generates
 * clocks internally but they never reach the physical pins.
 * SPIM00 (LP domain) can drive P2 pins → use SPIM00.
 *
 * SPIM00 has a 128 MHz core clock; prescaler=64 gives 2 MHz SPI.
 *
 * All transfers are full-duplex (TX.MAXCNT == RX.MAXCNT >= 1).
 *
 * ADS1220 SPI Mode 1: CPOL=0, CPHA=1
 *   CONFIG = 0x02 (MSB first, trailing edge latch, active-high clock)
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ads1220.h"
#include "gpio.h"
#include "board.h"     /* BOARD_SPI_* pin definitions */
#include "nrf.h"       /* pulls in NRF_SPIM00 */

#define DEBUG_LOG_MODULE_NAME "ADS"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO
#define DEBUG_LOG_UART_BAUDRATE 1000000
#include "debug_log.h"

/* ── SPIM00 configuration ───────────────────────────────────────────────────── */

/* SPIM00 core = 128 MHz; prescaler=64 → 2 MHz SPI clock */
#define SPIM00_PRESCALER      64u
#define SPIM00_CONFIG         0x02u   /* CPOL=0, CPHA=1, MSB first               */
#define SPIM00_ORC            0xFFu

/* PSEL pin encodings come straight from board.h (bits[7:5]=port, bits[4:0]=pin).
 * MISO and DRDY are SEPARATE pins: MISO = DOUT/DRDY data line (P2.09),
 * DRDY = dedicated data-ready line (P2.02). Do not confuse them. */
#define SPIM00_PSEL_SCK       BOARD_SPI_SCK_PIN     /* P2.06 */
#define SPIM00_PSEL_MOSI      BOARD_SPI_MOSI_PIN    /* P2.08 */
#define SPIM00_PSEL_MISO      BOARD_SPI_MISO_PIN    /* P2.09 */
#define SPIM00_PSEL_DISCONNECT 0x80000000UL

#define SPIM00_ENABLE_ON      0x07u
#define SPIM00_ENABLE_OFF     0x00u

/* Timeout: 2 MHz, 8 bytes = 32 µs = ~2048 cycles @ 64 MHz.
 * Each iteration reads a peripheral register (~10+ cycles).
 * 20000 gives a ~3 ms safety margin. */
#define SPIM00_XFER_TIMEOUT   20000UL

/* ── Private state ───────────────────────────────────────────────────────────── */

static bool    m_initialized = false;
static uint8_t m_cs_id;
static uint8_t m_drdy_id;
static uint8_t m_reg0_cache = 0;

/* Scratch buffers — word-aligned for EasyDMA.
 * Max transfer: WREG 4 regs = 1 cmd + 4 data = 5 bytes. */
static uint8_t s_tx[8] __attribute__((aligned(4)));
static uint8_t s_rx[8] __attribute__((aligned(4)));

/* ── CS helpers ─────────────────────────────────────────────────────────────── */

static void cs_assert(void)   { Gpio_outputWrite(m_cs_id, GPIO_LEVEL_LOW);  }
static void cs_deassert(void) { Gpio_outputWrite(m_cs_id, GPIO_LEVEL_HIGH); }

/* ── Raw SPIM00 transfer ─────────────────────────────────────────────────────── */

static bool s_first_xfer = true;

static ads1220_res_e spim00_xfer(const uint8_t * tx_buf,
                                  uint8_t       * rx_buf,
                                  uint32_t        len)
{
    /* SPIM00 stays enabled (set once in init) — the AIN2-only build ran for
     * hours this way; toggling ENABLE per transfer is what disturbed the radio. */
    NRF_SPIM00->EVENTS_END    = 0;
    NRF_SPIM00->DMA.TX.PTR    = (uint32_t)(uintptr_t)tx_buf;
    NRF_SPIM00->DMA.TX.MAXCNT = len;
    NRF_SPIM00->DMA.RX.PTR    = (uint32_t)(uintptr_t)rx_buf;
    NRF_SPIM00->DMA.RX.MAXCNT = len;
    NRF_SPIM00->TASKS_START   = 1;

    volatile uint32_t to = SPIM00_XFER_TIMEOUT;
    while (!NRF_SPIM00->EVENTS_END && to > 0u) { to--; }
    NRF_SPIM00->EVENTS_END = 0;

    if (s_first_xfer)
    {
        s_first_xfer = false;
        uint32_t tx_amt = NRF_SPIM00->DMA.TX.AMOUNT;
        uint32_t rx_amt = NRF_SPIM00->DMA.RX.AMOUNT;
        uint32_t tx_err = NRF_SPIM00->EVENTS_DMA.TX.BUSERROR;
        uint32_t rx_err = NRF_SPIM00->EVENTS_DMA.RX.BUSERROR;
        LOG(LVL_INFO, "SPIM00 xfer len=%u to=%u txAmt=%u rxAmt=%u txErr=%u rxErr=%u",
            (unsigned)len, (unsigned)to,
            (unsigned)tx_amt, (unsigned)rx_amt,
            (unsigned)tx_err, (unsigned)rx_err);
    }

    return (to > 0u) ? ADS1220_RES_OK : ADS1220_RES_SPI_ERR;
}

static ads1220_res_e spi_cmd_read(const uint8_t * cmd_buf,  uint32_t cmd_len,
                                   uint8_t       * out_data, uint32_t read_len)
{
    uint32_t total = cmd_len + read_len;
    if (total > (uint32_t)sizeof(s_tx)) return ADS1220_RES_SPI_ERR;

    memcpy(s_tx, cmd_buf, cmd_len);
    memset(s_tx + cmd_len, SPIM00_ORC, read_len);

    ads1220_res_e r = spim00_xfer(s_tx, s_rx, total);
    if (r == ADS1220_RES_OK)
        memcpy(out_data, s_rx + cmd_len, read_len);

    return r;
}

static ads1220_res_e spi_write(const uint8_t * write_buf, uint32_t len)
{
    if (len > (uint32_t)sizeof(s_tx)) return ADS1220_RES_SPI_ERR;
    memcpy(s_tx, write_buf, len);
    return spim00_xfer(s_tx, s_rx, len);
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

    /* DRDY: pull-up input (active-low; idle HIGH when chip has no new data). */
    gpio_in_cfg_t drdy_cfg = {
        .event_cb    = NULL,
        .event_cfg   = GPIO_IN_EVENT_NONE,
        .in_mode_cfg = GPIO_IN_PULL_UP,
    };
    Gpio_inputSetCfg(m_drdy_id, &drdy_cfg);

    /* ── Configure SPIM00 (LP domain — same domain as P2) ── */

    /* GPIO direction must be set before PSEL takes effect.
     * SCK and MOSI: output, idle LOW.  MISO stays input (reset default). */
    uint32_t sck_bit  = 1u << (BOARD_SPI_SCK_PIN  & 0x1Fu);
    uint32_t mosi_bit = 1u << (BOARD_SPI_MOSI_PIN & 0x1Fu);
    NRF_P2->OUTCLR = sck_bit | mosi_bit;
    NRF_P2->DIRSET = sck_bit | mosi_bit;

    NRF_SPIM00->ENABLE = SPIM00_ENABLE_OFF;

    NRF_SPIM00->PSEL.SCK  = SPIM00_PSEL_SCK;
    NRF_SPIM00->PSEL.MOSI = SPIM00_PSEL_MOSI;
    NRF_SPIM00->PSEL.MISO = SPIM00_PSEL_MISO;
    NRF_SPIM00->PSEL.CSN  = SPIM00_PSEL_DISCONNECT;

    NRF_SPIM00->PRESCALER = SPIM00_PRESCALER;
    NRF_SPIM00->CONFIG    = SPIM00_CONFIG;
    NRF_SPIM00->ORC       = SPIM00_ORC;

    NRF_SPIM00->EVENTS_STARTED = 0;
    NRF_SPIM00->EVENTS_END     = 0;
    NRF_SPIM00->EVENTS_STOPPED = 0;

    NRF_SPIM00->ENABLE = SPIM00_ENABLE_ON;

    uint32_t en   = NRF_SPIM00->ENABLE;
    uint32_t sck  = NRF_SPIM00->PSEL.SCK;
    uint32_t mosi = NRF_SPIM00->PSEL.MOSI;
    uint32_t miso = NRF_SPIM00->PSEL.MISO;
    uint32_t prsc = NRF_SPIM00->PRESCALER;
    LOG(LVL_INFO, "SPIM00 EN=%u SCK=%u MOSI=%u MISO=%u PRSC=%u",
        (unsigned)en, (unsigned)sck, (unsigned)mosi, (unsigned)miso, (unsigned)prsc);

    s_first_xfer = true;

    /* RESET command */
    uint8_t reset_cmd = ADS1220_CMD_RESET;
    cs_assert();
    ads1220_res_e r = spi_write(&reset_cmd, 1u);
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

    uint32_t ticks = timeout_ms * 1000u;
    gpio_level_e level;
    do {
        Gpio_inputRead(m_drdy_id, &level);
        if (ticks-- == 0u) return ADS1220_RES_NOT_READY;
    } while (level != GPIO_LEVEL_LOW);

    uint8_t cmd    = ADS1220_CMD_RDATA;
    uint8_t raw[3] = {0, 0, 0};

    cs_assert();
    ads1220_res_e r = spi_cmd_read(&cmd, 1u, raw, 3u);
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
    cs_assert();
    ads1220_res_e r = spi_cmd_read(&cmd, 1u, regs->reg, 4u);
    cs_deassert();
    return r;
}

ads1220_res_e ADS1220_set_mux(uint8_t mux)
{
    if (!m_initialized) return ADS1220_RES_NOT_INITIALIZED;
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
