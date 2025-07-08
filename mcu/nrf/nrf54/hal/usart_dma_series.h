/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/** Define NRF_UARTE0 and UART0_IRQn for the nRF54 series. */
#define NRF_UARTE0 NRF_UARTE20
#define UART0_IRQn UARTE20_IRQn

/* Define TASKS for the nRF54 series. */
#define NRF_UARTE0_TASKS_STARTTX (NRF_UARTE0->TASKS_DMA.TX.START)
#define NRF_UARTE0_TASKS_STOPTX  (NRF_UARTE0->TASKS_DMA.TX.STOP)
#define NRF_UARTE0_TASKS_STARTRX (NRF_UARTE0->TASKS_DMA.RX.START)
#define NRF_UARTE0_TASKS_STOPRX  (NRF_UARTE0->TASKS_DMA.RX.STOP)

/* Define EVENTS for the nRF54 series. */
#define NRF_UARTE0_EVENTS_ENDTX     (NRF_UARTE0->EVENTS_DMA.TX.END)
#define NRF_UARTE0_EVENTS_ENDRX     (NRF_UARTE0->EVENTS_DMA.RX.END)
#define NRF_UARTE0_EVENTS_RXSTARTED (NRF_UARTE0->EVENTS_DMA.RX.READY)

/* Define RX/TX buffer registers for the nRF54 series. */
#define NRF_UARTE0_RX_PTR    (NRF_UARTE0->DMA.RX.PTR)
#define NRF_UARTE0_RX_MAXCNT (NRF_UARTE0->DMA.RX.MAXCNT)
#define NRF_UARTE0_RX_AMOUNT (NRF_UARTE0->DMA.RX.AMOUNT)
#define NRF_UARTE0_TX_PTR    (NRF_UARTE0->DMA.TX.PTR)
#define NRF_UARTE0_TX_MAXCNT (NRF_UARTE0->DMA.TX.MAXCNT)

/* Define interrupts for the nRF54 series. */
#define NRF_UARTE0_INTENSET                                                    \
    ((UARTE_INTEN_DMATXEND_Enabled << UARTE_INTEN_DMATXEND_Pos)                \
     | (UARTE_INTEN_DMARXEND_Enabled << UARTE_INTEN_DMARXEND_Pos)              \
     | (UARTE_INTEN_RXTO_Enabled << UARTE_INTEN_RXTO_Pos)                      \
     | (UARTE_INTEN_ERROR_Enabled << UARTE_INTEN_ERROR_Pos))
