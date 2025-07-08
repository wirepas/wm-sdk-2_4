/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

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
