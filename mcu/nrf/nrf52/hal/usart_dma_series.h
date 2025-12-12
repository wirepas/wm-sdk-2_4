/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef NRF_USART_DMA_SERIES_H
#define NRF_USART_DMA_SERIES_H

/** Declare buffer size defined by max DMA transfer size, used by doublebuffer.h
 *  DSAP-DATA_TX_FRAG.request is assumed to cause largest SLIP frame
 *  - At the start of SLIP frame there can be 6 wakeup characters
 *  - Primitive ID 1 byte (content always 0x0f, thus cannot be escaped by SLIP)
 *  - Frame ID 1+1 bytes (can contain any value from 0 - 255,
 *                        thus can be 0xC0 or 0xDB that needs to be escaped by SLIP,
 *                        thus may require 2 bytes at UART level)
 *  - PDU ID 2+2 bytes
 *  - Source EP 1+1 bytes
 *  - Destination Address 4+4 bytes
 *  - Destination EP 1+1 bytes
 *  - QoS 1 byte (can be either 0 or 1, thus cannot be escaped by SLIP)
 *  - Tx Options 1 byte (most significant bit is always zero, thus cannot be escaped by SLIP)
 *  - Reserved 4+4 bytes (not mandated to be zero)
 *  - FullPacketId 2+2 bytes
 *  - Fragment and flags 2+2 bytes (bit 14 is reserved, not mandated to be zero)
 *  - APDU Length 1 byte (1 - 102, thus cannot be 0xC0 or 0xDB that needs to be escaped by SLIP)
 *  - APDU 102+102 bytes
 *  - CRC 2+2 bytes
 *  - SLIP frame END 1 byte
 *  Maximum size of SLIP frame is 253 bytes.
 *  Note! Problems caused by too short buffer aren't visible with low load.
 *        These problems becomes visible when interrupt triggered from end of DMA transfer
 *        cannot be processed fast enough, due higher priority interrupts (e.g. Radio, RTC),
 *        so that next DMA transfer would start before any bytes from SLIP frame is lost.
 *        That is why single DMA transfer should cover whole SLIP frame.
 */
#define BUFFER_SIZE 256u

/* Buffer used for reception
 * Rx happens in a circular buffer and is limited to 255 bytes as it is
 * the maximum DMA RX transfer available
 */
#define RX_BUFFER_SIZE                 255u

/* Define TASKS for the nRF52 series. */
#define NRF_UARTE0_TASKS_STARTTX (NRF_UARTE0->TASKS_STARTTX)
#define NRF_UARTE0_TASKS_STOPTX  (NRF_UARTE0->TASKS_STOPTX)
#define NRF_UARTE0_TASKS_STARTRX (NRF_UARTE0->TASKS_STARTRX)
#define NRF_UARTE0_TASKS_STOPRX  (NRF_UARTE0->TASKS_STOPRX)

/* Define EVENTS for the nRF52 series. */
#define NRF_UARTE0_EVENTS_ENDTX     (NRF_UARTE0->EVENTS_ENDTX)
#define NRF_UARTE0_EVENTS_ENDRX     (NRF_UARTE0->EVENTS_ENDRX)
#define NRF_UARTE0_EVENTS_RXSTARTED (NRF_UARTE0->EVENTS_RXSTARTED)

/* Define RX/TX buffer registers for the nRF52 series. */
#define NRF_UARTE0_RX_PTR    (NRF_UARTE0->RXD.PTR)
#define NRF_UARTE0_RX_MAXCNT (NRF_UARTE0->RXD.MAXCNT)
#define NRF_UARTE0_TX_PTR    (NRF_UARTE0->TXD.PTR)
#define NRF_UARTE0_TX_MAXCNT (NRF_UARTE0->TXD.MAXCNT)

/* Define interrupts for the nRF52 series. */
#define NRF_UARTE0_INTENSET                                                    \
    ((UARTE_INTEN_ENDTX_Enabled << UARTE_INTEN_ENDTX_Pos)                      \
     | (UARTE_INTEN_ERROR_Enabled << UARTE_INTEN_ERROR_Pos))

/* Define shortcuts for the nRF52 series. */
#define NRF_UARTE0_SHORTS                                                      \
    (UARTE_SHORTS_ENDRX_STARTRX_Enabled << UARTE_SHORTS_ENDRX_STARTRX_Pos)


/**
 * \brief   Configure USART timers for nRF52 devices.
 */
__attribute__((__always_inline__)) static inline void configure_timers(void)
{
    /* Configure PPI: 3 channels used, configured in a group */

    /* Create group */
    NRF_PPI->CHG[0] = (PPI_CHG_CH3_Included << PPI_CHG_CH3_Pos)
                      | (PPI_CHG_CH4_Included << PPI_CHG_CH4_Pos)
                      | (PPI_CHG_CH5_Included << PPI_CHG_CH5_Pos);

    /* Start Timer 1 when RX is started. Only used one time when starting RX */
    NRF_PPI->CH[3].EEP = (uint32_t) &NRF_UARTE0->EVENTS_RXSTARTED;
    NRF_PPI->CH[3].TEP = (uint32_t) &NRF_TIMER1->TASKS_START;

    /* Reset timer 1, each time a byte is received to avoid Timeout */
    /* Count the number of bytes received with Timer2 in count mode */
    NRF_PPI->CH[4].EEP   = (uint32_t) &NRF_UARTE0->EVENTS_RXDRDY;
    NRF_PPI->CH[4].TEP   = (uint32_t) &NRF_TIMER1->TASKS_CLEAR;
    NRF_PPI->FORK[4].TEP = (uint32_t) &NRF_TIMER2->TASKS_COUNT;

    /* Clear the Timer2 when ENDRX happens, ie buffer wrap*/
    NRF_PPI->CH[5].EEP = (uint32_t) &NRF_UARTE0->EVENTS_ENDRX;
    NRF_PPI->CH[5].TEP = (uint32_t) &NRF_TIMER2->TASKS_CLEAR;
}

#endif  // NRF_USART_DMA_SERIES_H
