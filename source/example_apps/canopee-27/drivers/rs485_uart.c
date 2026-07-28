/* RS485 UART driver — UARTE30 / SERIAL30 on nRF54L15 (canopee_v27)
 *
 * On canopee_v27 the RS485 signals are on P0 (RO=P0.00, DI=P0.02), which is the
 * low-power domain — only UARTE30 (SERIAL30) can PSEL to P0 pins. The console is
 * on P1.07/P1.08 via UARTE20 (peripheral domain, board/canopee_v27/
 * usart_dma_series.h), so the two UARTs coexist on different domains/instances.
 *
 * nRF54L15 PSEL encoding (same as NRF_PIN_PORT_TO_PIN_NUMBER):
 *   bits [4:0] = pin within port
 *   bits [7:5] = port number (0=P0, 1=P1, 2=P2)
 *   bit   31   = CONNECT (0=connected, 1=disconnected)
 *   → BOARD_RS485_UART_TX_PIN = 2 = P0.02
 *
 * nRF54L15 DMA register names differ from nRF52:
 *   TASKS_DMA.TX.START / .RX.START
 *   EVENTS_DMA.TX.END  / .RX.END
 *   DMA.TX.PTR / .MAXCNT  — DMA.RX.PTR / .MAXCNT / .AMOUNT
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "nrf.h"
#include "board.h"
#include "rs485_uart.h"

/* TX: generous timeout for DMA.TX.END (DMA reads RAM → TX FIFO, very fast).
 * STOP: 1 byte-time safety margin after DMA.TX.END before assuming shift reg empty.
 *   At 64 MHz, 1 byte@115200 = ~5600 cycles; 20000 iter @ 3 cycles/iter = ~312 µs ≈ 3.5 byte-times. */
#define RS485_TX_DMA_TIMEOUT_ITER   200000UL
#define RS485_TX_STOP_TIMEOUT_ITER   20000UL

/* RX buffer — word-aligned for DMA, large enough for any motor reply frame.
 * EasyDMA writes each received byte into this buffer as it arrives; the
 * DMA.RX.AMOUNT counter only latches at STOP/END, so we never rely on it for
 * mid-frame progress.  Instead the buffer is cleared to 0x00 before each arm and
 * scanned live for a complete STX..END protocol frame (see rs485_uart_rx_frame). */
#define RS485_RX_BUF_LEN  32U
static uint8_t m_rx_buf[RS485_RX_BUF_LEN] __attribute__((aligned(4)));

/* Extract NRF_Px GPIO bank from the Wirepas/nRF54L15 pin encoding. */
static NRF_GPIO_Type * gpio_port(uint32_t pin)
{
    switch ((pin >> 5u) & 0x7u)
    {
        case 1u: return NRF_P1;
        case 2u: return NRF_P2;
        default: return NRF_P0;
    }
}

void rs485_uart_init(void)
{
    NRF_UARTE30->ENABLE = UARTE_ENABLE_ENABLE_Disabled;

    /* GPIO — TX: push-pull output, idle high.
     * RX: input, no pull (bus terminated externally). */
    uint32_t tx_bit = 1UL << (BOARD_RS485_UART_TX_PIN & 0x1FU);
    uint32_t rx_bit = 1UL << (BOARD_RS485_UART_RX_PIN & 0x1FU);
    NRF_GPIO_Type *tx_port = gpio_port(BOARD_RS485_UART_TX_PIN);
    NRF_GPIO_Type *rx_port = gpio_port(BOARD_RS485_UART_RX_PIN);

    tx_port->OUTSET = tx_bit;
    tx_port->DIRSET = tx_bit;
    rx_port->DIRCLR = rx_bit;

    NRF_UARTE30->PSEL.TXD = BOARD_RS485_UART_TX_PIN;
    NRF_UARTE30->PSEL.RXD = BOARD_RS485_UART_RX_PIN;
    NRF_UARTE30->PSEL.CTS = 0xFFFFFFFFUL;
    NRF_UARTE30->PSEL.RTS = 0xFFFFFFFFUL;

    NRF_UARTE30->BAUDRATE = UARTE_BAUDRATE_BAUDRATE_Baud115200;

    /* 8N1, no flow control.
     * FRAMETIMEOUT_ENABLED: fires EVENTS_FRAMETIMEOUT after N idle bits on RX,
     * allowing the poll loop to detect end-of-frame without knowing frame length. */
    NRF_UARTE30->CONFIG =
          (UARTE_CONFIG_HWFC_Disabled         << UARTE_CONFIG_HWFC_Pos)
        | (UARTE_CONFIG_PARITY_Excluded       << UARTE_CONFIG_PARITY_Pos)
        | (UARTE_CONFIG_STOP_One              << UARTE_CONFIG_STOP_Pos)
        | (UARTE_CONFIG_PARITYTYPE_Even       << UARTE_CONFIG_PARITYTYPE_Pos)
        | (UARTE_CONFIG_FRAMESIZE_8bit        << UARTE_CONFIG_FRAMESIZE_Pos)
        | (UARTE_CONFIG_ENDIAN_MSB            << UARTE_CONFIG_ENDIAN_Pos)
        | (UARTE_CONFIG_FRAMETIMEOUT_ENABLED  << UARTE_CONFIG_FRAMETIMEOUT_Pos);

    /* 1000 bit-periods idle = ~8.7 ms — motor fw can have multi-ms inter-byte gaps */
    NRF_UARTE30->FRAMETIMEOUT = 1000U;

    NRF_UARTE30->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
}

bool rs485_uart_send(const uint8_t * data, uint8_t len)
{
    if (len == 0) return true;

    /* Stop any running RX DMA before driving the bus. */
    NRF_UARTE30->TASKS_DMA.RX.STOP = 1;

    /* Clear events, arm TX DMA, start. */
    NRF_UARTE30->EVENTS_DMA.TX.END  = 0;
    NRF_UARTE30->EVENTS_TXSTOPPED   = 0;
    NRF_UARTE30->DMA.TX.PTR         = (uint32_t)(uintptr_t)data;
    NRF_UARTE30->DMA.TX.MAXCNT      = len;
    NRF_UARTE30->TASKS_DMA.TX.START = 1;

    /* Wait for DMA to finish reading RAM → TX FIFO. */
    volatile uint32_t to = RS485_TX_DMA_TIMEOUT_ITER;
    while (!NRF_UARTE30->EVENTS_DMA.TX.END && to > 0)
    {
        to--;
    }
    bool dma_ok = (to > 0u);

    /* Short wait for the TX shift register to empty (~1 byte-time).
     * EVENTS_TXSTOPPED may not fire on nRF54L15 after TASKS_DMA.TX.STOP;
     * the loop gives a guaranteed minimum dwell of RS485_TX_STOP_TIMEOUT_ITER. */
    NRF_UARTE30->TASKS_DMA.TX.STOP = 1;
    to = RS485_TX_STOP_TIMEOUT_ITER;
    while (!NRF_UARTE30->EVENTS_TXSTOPPED && to > 0)
    {
        to--;
    }

    return dma_ok;
}

void rs485_uart_rx_arm(void)
{
    /* Fresh frame: stop, clear events, clear buffer, point DMA at start, START.
     * Clearing to 0x00 keeps not-yet-received positions distinct from STX/END. */
    NRF_UARTE30->TASKS_DMA.RX.STOP  = 1;
    NRF_UARTE30->EVENTS_DMA.RX.END  = 0;
    NRF_UARTE30->EVENTS_FRAMETIMEOUT = 0;
    for (uint32_t i = 0; i < RS485_RX_BUF_LEN; i++)
    {
        m_rx_buf[i] = 0u;
    }
    NRF_UARTE30->DMA.RX.PTR    = (uint32_t)(uintptr_t)m_rx_buf;
    NRF_UARTE30->DMA.RX.MAXCNT = RS485_RX_BUF_LEN;
    NRF_UARTE30->TASKS_DMA.RX.START = 1;
}

bool rs485_uart_rx_event(void)
{
    return (NRF_UARTE30->EVENTS_FRAMETIMEOUT != 0u)
        || (NRF_UARTE30->EVENTS_DMA.RX.END   != 0u);
}

uint8_t rs485_uart_rx_stop(void)
{
    /* STOP triggers FIFO→RAM flush; AMOUNT is only valid once END fires. */
    NRF_UARTE30->TASKS_DMA.RX.STOP = 1;
    volatile uint32_t to = 20000UL;
    while (!NRF_UARTE30->EVENTS_DMA.RX.END && to > 0u) { to--; }
    uint8_t amt = (uint8_t)(NRF_UARTE30->DMA.RX.AMOUNT & 0xFFu);
    NRF_UARTE30->EVENTS_FRAMETIMEOUT = 0;
    NRF_UARTE30->EVENTS_DMA.RX.END   = 0;
    return amt;
}

void rs485_uart_rx_resume(uint8_t offset)
{
    if (offset >= RS485_RX_BUF_LEN)
    {
        offset = 0u;   /* safety — should not happen for valid frames */
    }
    NRF_UARTE30->DMA.RX.PTR    = (uint32_t)(uintptr_t)(m_rx_buf + offset);
    NRF_UARTE30->DMA.RX.MAXCNT = (uint16_t)(RS485_RX_BUF_LEN - offset);
    NRF_UARTE30->EVENTS_FRAMETIMEOUT = 0;
    NRF_UARTE30->EVENTS_DMA.RX.END   = 0;
    NRF_UARTE30->TASKS_DMA.RX.START  = 1;
}

uint8_t rs485_uart_rx_check(uint8_t total, const uint8_t ** out)
{
    if (out != NULL)
    {
        *out = m_rx_buf;   /* expose buffer even when incomplete (for diagnostics) */
    }
    if (total < 5u)            return 0u;   /* too short for STX|ADDR|CMD|NBR|END */
    if (m_rx_buf[0] != 0x02u)  return 0u;   /* no STX */
    uint16_t len = (uint16_t)m_rx_buf[3] + 5u;
    if (len > total || len > RS485_RX_BUF_LEN) return 0u;  /* not all bytes in yet */
    if (m_rx_buf[len - 1u] != 0x03u)           return 0u;  /* END not in place */
    return (uint8_t)len;
}
