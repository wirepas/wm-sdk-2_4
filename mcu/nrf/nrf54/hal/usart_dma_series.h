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
#define NRF_UARTE0_INTENSET                                                     \
    ((UARTE_INTEN_DMATXEND_Enabled << UARTE_INTEN_DMATXEND_Pos)                 \
     | (UARTE_INTEN_DMARXEND_Enabled << UARTE_INTEN_DMARXEND_Pos)               \
     | (UARTE_INTEN_DMARXREADY_Enabled << UARTE_INTEN_DMARXREADY_Pos)           \
     | (UARTE_INTEN_ERROR_Enabled << UARTE_INTEN_ERROR_Pos))

/* Default config for UARTE, hardware flow control DISABLED */
#define NRF_UARTE0_CONFIG_DEFAULT                                               \
    ((UARTE_CONFIG_HWFC_Disabled << UARTE_CONFIG_HWFC_Pos)                      \
     | (UARTE_CONFIG_PARITY_Excluded << UARTE_CONFIG_PARITY_Pos)                \
     | (UARTE_CONFIG_STOP_One << UARTE_CONFIG_STOP_Pos)                         \
     | (UARTE_CONFIG_PARITYTYPE_Even << UARTE_CONFIG_PARITYTYPE_Pos)            \
     | (UARTE_CONFIG_FRAMESIZE_8bit << UARTE_CONFIG_FRAMESIZE_Pos)              \
     | (UARTE_CONFIG_ENDIAN_MSB << UARTE_CONFIG_ENDIAN_Pos)                     \
     | (UARTE_CONFIG_FRAMETIMEOUT_ENABLED << UARTE_CONFIG_FRAMETIMEOUT_Pos))

/* Default config for UARTE, hardware flow control ENABLED */
#define NRF_UARTE0_CONFIG_DEFAULT_HWFC                                          \
    ((UARTE_CONFIG_HWFC_Enabled << UARTE_CONFIG_HWFC_Pos)                       \
     | (UARTE_CONFIG_PARITY_Excluded << UARTE_CONFIG_PARITY_Pos)                \
     | (UARTE_CONFIG_STOP_One << UARTE_CONFIG_STOP_Pos)                         \
     | (UARTE_CONFIG_PARITYTYPE_Even << UARTE_CONFIG_PARITYTYPE_Pos)            \
     | (UARTE_CONFIG_FRAMESIZE_8bit << UARTE_CONFIG_FRAMESIZE_Pos)              \
     | (UARTE_CONFIG_ENDIAN_MSB << UARTE_CONFIG_ENDIAN_Pos)                     \
     | (UARTE_CONFIG_FRAMETIMEOUT_ENABLED << UARTE_CONFIG_FRAMETIMEOUT_Pos))

/* UARTE shortcut configuration */
#define NRF_UARTE0_SHORTS                                                       \
    (UARTE_SHORTS_FRAMETIMEOUT_DMA_RX_STOP_Enabled << UARTE_SHORTS_FRAMETIMEOUT_DMA_RX_STOP_Pos)

#endif  // NRF_USART_DMA_SERIES_H
