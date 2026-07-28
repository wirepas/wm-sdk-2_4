/* canopee_v27 UART series override:
 * The console is on P1.07/P1.08 (peripheral domain) → UARTE20 (SERIAL20).
 * (RS485 uses the LP-domain UARTE30 on P0, driven directly by its own driver.)
 * Remap the Wirepas debug-UART HAL (NRF_UARTE0) onto UARTE20.
 */
#ifndef NRF_USART_DMA_SERIES_H
#define NRF_USART_DMA_SERIES_H

#define BUFFER_SIZE 256u

#define NRF_UARTE0  NRF_UARTE20
#define UART0_IRQn  UARTE20_IRQn

#define NRF_UARTE0_TASKS_STARTTX (NRF_UARTE0->TASKS_DMA.TX.START)
#define NRF_UARTE0_TASKS_STOPTX  (NRF_UARTE0->TASKS_DMA.TX.STOP)
#define NRF_UARTE0_TASKS_STARTRX (NRF_UARTE0->TASKS_DMA.RX.START)
#define NRF_UARTE0_TASKS_STOPRX  (NRF_UARTE0->TASKS_DMA.RX.STOP)

#define NRF_UARTE0_EVENTS_ENDTX     (NRF_UARTE0->EVENTS_DMA.TX.END)
#define NRF_UARTE0_EVENTS_ENDRX     (NRF_UARTE0->EVENTS_DMA.RX.END)
#define NRF_UARTE0_EVENTS_RXSTARTED (NRF_UARTE0->EVENTS_DMA.RX.READY)

#define NRF_UARTE0_RX_PTR    (NRF_UARTE0->DMA.RX.PTR)
#define NRF_UARTE0_RX_MAXCNT (NRF_UARTE0->DMA.RX.MAXCNT)
#define NRF_UARTE0_RX_AMOUNT (NRF_UARTE0->DMA.RX.AMOUNT)
#define NRF_UARTE0_TX_PTR    (NRF_UARTE0->DMA.TX.PTR)
#define NRF_UARTE0_TX_MAXCNT (NRF_UARTE0->DMA.TX.MAXCNT)

#define NRF_UARTE0_INTENSET                                                      \
    ((UARTE_INTEN_DMATXEND_Enabled   << UARTE_INTEN_DMATXEND_Pos)               \
     | (UARTE_INTEN_DMARXEND_Enabled   << UARTE_INTEN_DMARXEND_Pos)             \
     | (UARTE_INTEN_DMARXREADY_Enabled << UARTE_INTEN_DMARXREADY_Pos)           \
     | (UARTE_INTEN_ERROR_Enabled      << UARTE_INTEN_ERROR_Pos))

#define NRF_UARTE0_CONFIG_DEFAULT                                                \
    ((UARTE_CONFIG_HWFC_Disabled       << UARTE_CONFIG_HWFC_Pos)                \
     | (UARTE_CONFIG_PARITY_Excluded   << UARTE_CONFIG_PARITY_Pos)              \
     | (UARTE_CONFIG_STOP_One          << UARTE_CONFIG_STOP_Pos)                \
     | (UARTE_CONFIG_PARITYTYPE_Even   << UARTE_CONFIG_PARITYTYPE_Pos)          \
     | (UARTE_CONFIG_FRAMESIZE_8bit    << UARTE_CONFIG_FRAMESIZE_Pos)           \
     | (UARTE_CONFIG_ENDIAN_MSB        << UARTE_CONFIG_ENDIAN_Pos)              \
     | (UARTE_CONFIG_FRAMETIMEOUT_ENABLED << UARTE_CONFIG_FRAMETIMEOUT_Pos))

#define NRF_UARTE0_CONFIG_DEFAULT_HWFC                                           \
    ((UARTE_CONFIG_HWFC_Enabled        << UARTE_CONFIG_HWFC_Pos)                \
     | (UARTE_CONFIG_PARITY_Excluded   << UARTE_CONFIG_PARITY_Pos)              \
     | (UARTE_CONFIG_STOP_One          << UARTE_CONFIG_STOP_Pos)                \
     | (UARTE_CONFIG_PARITYTYPE_Even   << UARTE_CONFIG_PARITYTYPE_Pos)          \
     | (UARTE_CONFIG_FRAMESIZE_8bit    << UARTE_CONFIG_FRAMESIZE_Pos)           \
     | (UARTE_CONFIG_ENDIAN_MSB        << UARTE_CONFIG_ENDIAN_Pos)              \
     | (UARTE_CONFIG_FRAMETIMEOUT_ENABLED << UARTE_CONFIG_FRAMETIMEOUT_Pos))

#define NRF_UARTE0_SHORTS \
    (UARTE_SHORTS_FRAMETIMEOUT_DMA_RX_STOP_Enabled << UARTE_SHORTS_FRAMETIMEOUT_DMA_RX_STOP_Pos)

#endif  /* NRF_USART_DMA_SERIES_H */
