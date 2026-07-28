/* RS485 UART driver — UARTE20 / SERIAL20 on nRF54L15
 *
 * TX = BOARD_RS485_UART_TX_PIN (P1.04 = pin 36)
 * RX = BOARD_RS485_UART_RX_PIN (P1.05 = pin 37)
 * DE = BOARD_RS485_DE_PIN, controlled by caller
 *
 * Baud rate is fixed at 115200 8N1 with FRAMETIMEOUT detection.
 */

#ifndef RS485_UART_H_
#define RS485_UART_H_

#include <stdint.h>
#include <stdbool.h>

/** Initialise UARTE20 for RS485 at 115200 8N1 with FRAMETIMEOUT. */
void rs485_uart_init(void);

/**
 * Transmit len bytes from data using DMA.
 * Stops any running RX DMA first.
 * Blocks until all bytes have been shifted out (~1.3 ms for 15 B at 115200).
 * data must reside in RAM (not flash).
 * Returns true if DMA completed, false on timeout.
 */
bool rs485_uart_send(const uint8_t * data, uint8_t len);

/**
 * Start receiving a fresh reply: clear the internal buffer to 0x00, point the
 * DMA at its start and START. Call after rs485_uart_send() and after each frame
 * (or timeout) to re-arm for the next reply.
 *
 * On nRF54L15 the UARTE holds received bytes in an internal FIFO and only flushes
 * them to RAM (and latches DMA.RX.AMOUNT) on STOP/END — so a frame is read in
 * fragments delimited by FRAMETIMEOUT, reassembled in-place via rx_resume().
 */
void rs485_uart_rx_arm(void);

/** True if a fragment-boundary event (FRAMETIMEOUT or DMA.RX.END) is pending. */
bool rs485_uart_rx_event(void);

/**
 * Stop the current DMA run (flushing the FIFO to RAM and latching the count),
 * clear the boundary events, and return the number of bytes received in this run.
 */
uint8_t rs485_uart_rx_stop(void);

/**
 * Resume reception of the same frame, appending into the buffer at byte `offset`
 * (i.e. continue after a partial fragment). Clears boundary events and STARTs.
 */
void rs485_uart_rx_resume(uint8_t offset);

/**
 * Check the buffer (now valid in RAM after rx_stop) for a complete motor frame
 *   STX(0x02) | ADDR | CMD | NBR_DATA | DATA[NBR_DATA] | END(0x03)
 * given `total` bytes received so far. Returns the frame length or 0 if not yet
 * complete. If out != NULL and complete, *out points at the internal buffer.
 */
uint8_t rs485_uart_rx_check(uint8_t total, const uint8_t ** out);

#endif /* RS485_UART_H_ */
