/* NFC hardware driver for nRF54L15.
 * Adapted from source/unitary_apps/nfc/drivers/nrf52/nfc_hw.c (nRF52840 section).
 *
 * nRF54L15 differences vs nRF52840:
 *   - NFCID1 registers are struct members: NFCID1.SECONDLAST / NFCID1.LAST
 *   - No NFC_POWER (FTPAN-116) workaround
 *   - No NFC_DEFAULTSTATESLEEP register — always sleep to SLEEP_A state
 *   - FTPAN-190 activation state machine replaced by a simple spin-wait for HFXO
 *     (no hw_delay needed — avoids conflict with the Wirepas stack's RTC use)
 *   - POWER_RESETREAS has no NFC bit
 */

#include <stdlib.h>

#include "api.h"
#include "mcu.h"
#include "nfc_hw.h"

#define LSB_32(a)  ((a) & 0x000000FFu)

#define NFC_FIELDPRESENT_MASK    (NFCT_FIELDPRESENT_LOCKDETECT_Msk | \
                                  NFCT_FIELDPRESENT_FIELDPRESENT_Msk)

#define NFC_FRAMESTATUS_RX_MSK   (NFCT_FRAMESTATUS_RX_OVERRUN_Msk      | \
                                  NFCT_FRAMESTATUS_RX_PARITYSTATUS_Msk  | \
                                  NFCT_FRAMESTATUS_RX_CRCERROR_Msk)

#define NFC_ERRORSTATUS_ALL      NFCT_ERRORSTATUS_FRAMEDELAYTIMEOUT_Msk

#define NFCID1_2ND_LAST_BYTE2_SHIFT  16u
#define NFCID1_2ND_LAST_BYTE1_SHIFT   8u
#define NFCID1_2ND_LAST_BYTE0_SHIFT   0u

#define NFCID1_LAST_BYTE3_SHIFT  24u
#define NFCID1_LAST_BYTE2_SHIFT  16u
#define NFCID1_LAST_BYTE1_SHIFT   8u
#define NFCID1_LAST_BYTE0_SHIFT   0u

#define CASCADE_TAG_BYTE         0x88u
#define NFC_CRC_SIZE             2u
#define NFC_BUFFER_SIZE          16u
#define NFC_TAG_MIN_SIZE         46
#define NFC_SLP_REQ_CMD          0x50u

/* Maximum spin iterations waiting for HFXO (~500 µs at Cortex-M33 64 MHz).
 * HFXO typically starts within 350 µs. */
#define HFXO_WAIT_LOOPS          5000u

static hw_nfc_callback_f         m_nfc_lib_callback = (hw_nfc_callback_f)NULL;
static uint8_t                   m_nfcid1_data[10]  = {0};
static volatile uint8_t          m_nfc_buffer[NFC_BUFFER_SIZE] = {0};
static volatile bool             m_nfc_field_on;
static volatile bool             m_slp_req_received;
static uint8_t                  *m_nfc_internal;
static uint8_t                   m_filter_field;
static bool                      m_isNfcInit = false;
static uint8_t                   m_Ack;

#pragma GCC push_options
#pragma GCC target("general-regs-only")
void __attribute__((__interrupt__)) IRQ_handler(void);
#pragma GCC pop_options

static void setup_nfc_header(void)
{
    uint32_t h0 = NRF_FICR->NFC.TAGHEADER0;
    uint32_t h1 = NRF_FICR->NFC.TAGHEADER1;
    uint32_t h2 = NRF_FICR->NFC.TAGHEADER2;

    m_nfcid1_data[0] = (uint8_t)LSB_32(h0 >> 0);
    m_nfcid1_data[1] = (uint8_t)LSB_32(h0 >> 8);
    m_nfcid1_data[2] = (uint8_t)LSB_32(h0 >> 16);
    m_nfcid1_data[3] = (uint8_t)LSB_32(h1 >> 0);
    m_nfcid1_data[4] = (uint8_t)LSB_32(h1 >> 8);
    m_nfcid1_data[5] = (uint8_t)LSB_32(h1 >> 16);
    m_nfcid1_data[6] = (uint8_t)LSB_32(h1 >> 24);
    m_nfcid1_data[7] = (uint8_t)LSB_32(h2 >> 0);
    m_nfcid1_data[8] = (uint8_t)LSB_32(h2 >> 8);
    m_nfcid1_data[9] = (uint8_t)LSB_32(h2 >> 16);

    if (m_nfcid1_data[3] == 0x88u)
    {
        m_nfcid1_data[3] |= 0x11u;
    }
}

static nfc_hw_res_e setup_nfc_reg(void)
{
    NRF_NFCT->INTENSET =
        (NFCT_INTENSET_FIELDDETECTED_Enabled << NFCT_INTENSET_FIELDDETECTED_Pos) |
        (NFCT_INTENSET_FIELDLOST_Enabled     << NFCT_INTENSET_FIELDLOST_Pos)     |
        (NFCT_INTENSET_ERROR_Enabled         << NFCT_INTENSET_ERROR_Pos)         |
        (NFCT_INTENSET_SELECTED_Enabled      << NFCT_INTENSET_SELECTED_Pos);

    NRF_NFCT->FRAMEDELAYMODE =
        (NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_WindowGrid <<
         NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_Pos) & NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_Msk;

    /* nRF54L15: NFCID1 registers are struct members */
    NRF_NFCT->NFCID1.SECONDLAST =
        ((uint32_t)m_nfcid1_data[0] << NFCID1_2ND_LAST_BYTE2_SHIFT) |
        ((uint32_t)m_nfcid1_data[1] << NFCID1_2ND_LAST_BYTE1_SHIFT) |
        ((uint32_t)m_nfcid1_data[2] << NFCID1_2ND_LAST_BYTE0_SHIFT);

    NRF_NFCT->NFCID1.LAST =
        ((uint32_t)m_nfcid1_data[3] << NFCID1_LAST_BYTE3_SHIFT) |
        ((uint32_t)m_nfcid1_data[4] << NFCID1_LAST_BYTE2_SHIFT) |
        ((uint32_t)m_nfcid1_data[5] << NFCID1_LAST_BYTE1_SHIFT) |
        ((uint32_t)m_nfcid1_data[6] << NFCID1_LAST_BYTE0_SHIFT);

    /* Bugfix for FTPAN-25 (IC-9929) */
    NRF_NFCT->SENSRES =
        (NFCT_SENSRES_NFCIDSIZE_NFCID1Double << NFCT_SENSRES_NFCIDSIZE_Pos) |
        (NFCT_SENSRES_BITFRAMESDD_SDD00100   << NFCT_SENSRES_BITFRAMESDD_Pos);

    return NFC_HW_RES_OK;
}

static void event_clear(volatile uint32_t *p_event)
{
    *p_event = 0;
    volatile uint32_t dummy = *p_event;
    (void)dummy;
}

/* Spin-wait for HFXO, called from IRQ context.
 * The Wirepas stack's RTC can't be relied upon here, so we busy-wait.
 * HFXO typically starts within 350 µs; HFXO_WAIT_LOOPS gives ~500 µs margin. */
static void wait_for_hfxo(void)
{
    bool active = false;
    for (uint32_t i = 0; i < HFXO_WAIT_LOOPS && !active; i++)
    {
        lib_hw->isPeripheralActivated(APP_LIB_HARDWARE_PERIPHERAL_HFXO, &active, 0);
    }
}

static void field_on(void)
{
    if (m_filter_field != 0u)
    {
        return;
    }

    m_filter_field++;
    m_nfc_field_on = true;

    lib_hw->activatePeripheral(APP_LIB_HARDWARE_PERIPHERAL_HFXO);
    wait_for_hfxo();

    NRF_NFCT->TASKS_ACTIVATE = 1;

    if (m_nfc_lib_callback != NULL)
    {
        m_nfc_lib_callback(NFC_HW_EVENT_FIELD_ON, 0, 0, 0);
    }
}

static void field_off(void)
{
    if (m_filter_field == 0u)
    {
        return;
    }

    m_nfc_field_on = false;
    m_filter_field--;

    NRF_NFCT->TASKS_SENSE = 1;

    lib_hw->deactivatePeripheral(APP_LIB_HARDWARE_PERIPHERAL_HFXO);

    NRF_NFCT->INTENCLR =
        (NFCT_INTENCLR_RXFRAMEEND_Clear << NFCT_INTENCLR_RXFRAMEEND_Pos) |
        (NFCT_INTENCLR_RXERROR_Clear    << NFCT_INTENCLR_RXERROR_Pos);

    /* No NFC_POWER workaround needed on nRF54L15 */
    setup_nfc_reg();

    if (m_nfc_lib_callback != NULL)
    {
        m_nfc_lib_callback(NFC_HW_EVENT_FIELD_OFF, 0, 0, 0);
    }
}

static void default_state_reset(void)
{
    /* On nRF54L15 always reset to SLEEP_A state */
    NRF_NFCT->TASKS_GOSLEEP = 1;

    NRF_NFCT->INTENCLR = NFCT_INTENCLR_RXFRAMEEND_Clear <<
                          NFCT_INTENCLR_RXFRAMEEND_Pos;
}

#pragma GCC push_options
#pragma GCC target("general-regs-only")
void __attribute__((__interrupt__)) IRQ_handler(void)
{
    nfc_hw_res_e ret       = NFC_HW_RES_OK;
    uint32_t     rx_status = 0;
    bool         field_detected = false;
    bool         field_lost     = false;

    if (NRF_NFCT->EVENTS_FIELDDETECTED &&
        (NRF_NFCT->INTEN & NFCT_INTEN_FIELDDETECTED_Msk))
    {
        event_clear(&NRF_NFCT->EVENTS_FIELDDETECTED);
        field_detected = true;
    }

    if (NRF_NFCT->EVENTS_FIELDLOST &&
        (NRF_NFCT->INTEN & NFCT_INTEN_FIELDLOST_Msk))
    {
        event_clear(&NRF_NFCT->EVENTS_FIELDLOST);
        field_lost = true;
    }

    /* Prioritise: if both events fire, treat as LOST (field came and went) */
    if (field_detected && !field_lost)
    {
        field_on();
    }
    else if (field_lost)
    {
        field_off();
    }

    if (NRF_NFCT->EVENTS_RXERROR &&
        (NRF_NFCT->INTEN & NFCT_INTEN_RXERROR_Msk))
    {
        rx_status = NRF_NFCT->FRAMESTATUS.RX;
        event_clear(&NRF_NFCT->EVENTS_RXERROR);
        NRF_NFCT->FRAMESTATUS.RX = NFC_FRAMESTATUS_RX_MSK;
    }

    if (NRF_NFCT->EVENTS_RXFRAMEEND &&
        (NRF_NFCT->INTEN & NFCT_INTEN_RXFRAMEEND_Msk))
    {
        uint32_t rx_data_size =
            (NRF_NFCT->RXD.AMOUNT & NFCT_RXD_AMOUNT_RXDATABYTES_Msk) >>
            NFCT_RXD_AMOUNT_RXDATABYTES_Pos;

        if (rx_data_size >= NFC_CRC_SIZE)
        {
            rx_data_size -= NFC_CRC_SIZE;
        }

        event_clear(&NRF_NFCT->EVENTS_RXFRAMEEND);

        if ((rx_data_size == 0) || (rx_data_size > NFC_BUFFER_SIZE) || rx_status)
        {
            ret = NFC_HW_RES_ERR;
        }
        else
        {
            if (m_nfc_buffer[0] == NFC_SLP_REQ_CMD)
            {
                m_slp_req_received = true;
                NRF_NFCT->INTENCLR = NFCT_INTENCLR_RXFRAMEEND_Clear <<
                                      NFCT_INTENCLR_RXFRAMEEND_Pos;
            }
            else
            {
                if (m_nfc_lib_callback != NULL)
                {
                    ret = m_nfc_lib_callback(NFC_HW_EVENT_DATA_RECEIVED,
                                             (void *)m_nfc_internal,
                                             (void *)m_nfc_buffer,
                                             rx_data_size);
                }
            }
        }
    }

    if (ret != NFC_HW_RES_OK)
    {
        default_state_reset();
    }

    if (NRF_NFCT->EVENTS_TXFRAMEEND &&
        (NRF_NFCT->INTEN & NFCT_INTEN_TXFRAMEEND_Msk))
    {
        event_clear(&NRF_NFCT->EVENTS_TXFRAMEEND);

        NRF_NFCT->INTENCLR = NFCT_INTENCLR_TXFRAMEEND_Clear <<
                              NFCT_INTENCLR_TXFRAMEEND_Pos;

        NRF_NFCT->PACKETPTR          = (uint32_t)m_nfc_buffer;
        NRF_NFCT->MAXLEN             = NFC_BUFFER_SIZE;
        NRF_NFCT->TASKS_ENABLERXDATA = 1;

        if (m_nfc_lib_callback != NULL)
        {
            m_nfc_lib_callback(NFC_HW_EVENT_DATA_TRANSMITTED, 0, 0, 0);
        }
    }

    if (NRF_NFCT->EVENTS_SELECTED &&
        (NRF_NFCT->INTEN & NFCT_INTEN_SELECTED_Msk))
    {
        event_clear(&NRF_NFCT->EVENTS_SELECTED);
        event_clear(&NRF_NFCT->EVENTS_RXFRAMEEND);
        event_clear(&NRF_NFCT->EVENTS_RXERROR);

        NRF_NFCT->PACKETPTR          = (uint32_t)m_nfc_buffer;
        NRF_NFCT->MAXLEN             = NFC_BUFFER_SIZE;
        NRF_NFCT->TASKS_ENABLERXDATA = 1;

        NRF_NFCT->INTENSET =
            (NFCT_INTENSET_RXFRAMEEND_Enabled << NFCT_INTENSET_RXFRAMEEND_Pos) |
            (NFCT_INTENSET_RXERROR_Enabled    << NFCT_INTENSET_RXERROR_Pos);

        NRF_NFCT->FRAMESTATUS.RX = NFC_FRAMESTATUS_RX_MSK;
        NRF_NFCT->ERRORSTATUS    = NFC_ERRORSTATUS_ALL;

        if (m_nfc_lib_callback != NULL)
        {
            m_nfc_lib_callback(NFC_HW_EVENT_SELECTED, 0, 0, 0);
        }
    }

    if (NRF_NFCT->EVENTS_ERROR &&
        (NRF_NFCT->INTEN & NFCT_INTEN_ERROR_Msk))
    {
        uint32_t err_status = NRF_NFCT->ERRORSTATUS;
        event_clear(&NRF_NFCT->EVENTS_ERROR);

        if ((err_status & NFCT_ERRORSTATUS_FRAMEDELAYTIMEOUT_Msk) &&
            m_slp_req_received)
        {
            NRF_NFCT->ERRORSTATUS = NFCT_ERRORSTATUS_FRAMEDELAYTIMEOUT_Msk;
            m_slp_req_received    = false;
        }

        NRF_NFCT->ERRORSTATUS = NFC_ERRORSTATUS_ALL;
    }
}
#pragma GCC pop_options


nfc_hw_res_e nfc_hw_init(hw_nfc_callback_f callback, uint8_t *buffer, size_t size)
{
    m_nfc_lib_callback = callback;

    if ((buffer == NULL) || (size < NFC_TAG_MIN_SIZE))
    {
        return NFC_HW_RES_INVALID_CONFIG;
    }

    m_nfc_internal = buffer;

    setup_nfc_header();
    setup_nfc_reg();

    m_nfc_internal[0]  = m_nfcid1_data[0];
    m_nfc_internal[1]  = m_nfcid1_data[1];
    m_nfc_internal[2]  = m_nfcid1_data[2];
    m_nfc_internal[3]  = (uint8_t)(CASCADE_TAG_BYTE ^
                          m_nfc_internal[0] ^ m_nfc_internal[1] ^ m_nfc_internal[2]);
    m_nfc_internal[4]  = m_nfcid1_data[3];
    m_nfc_internal[5]  = m_nfcid1_data[4];
    m_nfc_internal[6]  = m_nfcid1_data[5];
    m_nfc_internal[7]  = m_nfcid1_data[6];
    m_nfc_internal[8]  = (uint8_t)(m_nfc_internal[4] ^ m_nfc_internal[5] ^
                          m_nfc_internal[6] ^ m_nfc_internal[7]);
    m_nfc_internal[9]  = 0xFFu;
    m_nfc_internal[10] = 0x00u;
    m_nfc_internal[11] = 0x00u;
    m_nfc_internal[12] = 0xE1u;
    m_nfc_internal[13] = 0x11u;
    m_nfc_internal[14] = (uint8_t)((size - 32u) >> 3);
    m_nfc_internal[15] = 0x00u;

    return NFC_HW_RES_OK;
}


bool nfc_hw_is_field_on(void)
{
    return m_nfc_field_on;
}


nfc_hw_res_e nfc_hw_start(void)
{
    if (m_isNfcInit)
    {
        return NFC_HW_RES_ALREADY_INITIALIZED;
    }

    NRF_NFCT->ERRORSTATUS = NFC_ERRORSTATUS_ALL;
    NRF_NFCT->TASKS_SENSE = 1;

    lib_system->clearPendingFastAppIrq(NFCT_IRQn);
    lib_system->enableAppIrq(true, NFCT_IRQn, APP_LIB_SYSTEM_IRQ_PRIO_LO, IRQ_handler);

    m_isNfcInit = true;

    return NFC_HW_RES_OK;
}


nfc_hw_res_e nfc_hw_stop(void)
{
    if (!m_isNfcInit)
    {
        return NFC_HW_RES_NOT_INITIALIZED;
    }

    lib_hw->deactivatePeripheral(APP_LIB_HARDWARE_PERIPHERAL_HFXO);

    NRF_NFCT->TASKS_DISABLE = 1;

    lib_system->disableAppIrq(NFCT_IRQn);

    m_nfc_field_on  = false;
    m_filter_field  = 0;
    m_isNfcInit     = false;

    return NFC_HW_RES_OK;
}


nfc_hw_res_e nfc_hw_send(const uint8_t *pData, size_t dataLength)
{
    if (!m_nfc_field_on)
    {
        return NFC_HW_RES_INVALID_CONFIG;
    }

    if ((dataLength == 0) || (dataLength > NFC_BUFFER_SIZE))
    {
        return NFC_HW_RES_INVALID_CONFIG;
    }

    event_clear(&NRF_NFCT->EVENTS_TXFRAMEEND);

    NRF_NFCT->TXD.FRAMECONFIG =
        (NFCT_TXD_FRAMECONFIG_PARITY_Parity           << NFCT_TXD_FRAMECONFIG_PARITY_Pos)      |
        (NFCT_TXD_FRAMECONFIG_DISCARDMODE_DiscardStart << NFCT_TXD_FRAMECONFIG_DISCARDMODE_Pos) |
        (NFCT_TXD_FRAMECONFIG_SOF_SoF                 << NFCT_TXD_FRAMECONFIG_SOF_Pos)          |
        (NFCT_TXD_FRAMECONFIG_CRCMODETX_CRC16TX       << NFCT_TXD_FRAMECONFIG_CRCMODETX_Pos);

    NRF_NFCT->PACKETPTR  = (uint32_t)pData;
    NRF_NFCT->TXD.AMOUNT = (dataLength << NFCT_TXD_AMOUNT_TXDATABYTES_Pos) &
                            NFCT_TXD_AMOUNT_TXDATABYTES_Msk;
    NRF_NFCT->INTENSET   = NFCT_INTENSET_TXFRAMEEND_Enabled <<
                            NFCT_INTENSET_TXFRAMEEND_Pos;

    NRF_NFCT->TASKS_STARTTX = 1;

    return NFC_HW_RES_OK;
}


nfc_hw_res_e nfc_hw_send_ack(bool ack_nack)
{
    m_Ack = ack_nack ? 0x0Au : 0x00u;

    if (!m_nfc_field_on)
    {
        return NFC_HW_RES_INVALID_CONFIG;
    }

    event_clear(&NRF_NFCT->EVENTS_TXFRAMEEND);

    NRF_NFCT->PACKETPTR  = (uint32_t)(&m_Ack);
    NRF_NFCT->TXD.AMOUNT = (4u << NFCT_TXD_AMOUNT_TXDATABITS_Pos) &
                            NFCT_TXD_AMOUNT_TXDATABITS_Msk;
    NRF_NFCT->TXD.FRAMECONFIG = NFCT_TXD_FRAMECONFIG_SOF_SoF <<
                                  NFCT_TXD_FRAMECONFIG_SOF_Pos;
    NRF_NFCT->INTENSET   = NFCT_INTENSET_TXFRAMEEND_Enabled <<
                            NFCT_INTENSET_TXFRAMEEND_Pos;

    NRF_NFCT->TASKS_STARTTX = 1;

    return NFC_HW_RES_OK;
}


void nfc_hw_wake_from(void)
{
    /* nRF54L15: not used in Wirepas context */
    NRF_NFCT->TASKS_SENSE = 1;
}


bool nfc_hw_is_nfc_reset_reason(void)
{
    /* nRF54L15: no NFC reset reason bit in RESETREAS */
    return false;
}
