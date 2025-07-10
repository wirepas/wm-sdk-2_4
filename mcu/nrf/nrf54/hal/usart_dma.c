/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>

#include "board.h"
#include "hal_api.h"
#include "api.h"
#include "usart_dma_series.h"

/* Only one USART, this is easy */
static volatile serial_rx_callback_f m_rx_callback;

/** Declare buffer size (defined by max DMA transfer size) */
#define BUFFER_SIZE 256u
#include "doublebuffer.h"

/* Buffers used for transmission */
static double_buffer_t m_tx_buffers;

/* Is tx_ongoing ? */
static volatile bool m_tx_ongoing;

/* Size of RX buffers.
 *
 * By default RX buffer size is set to 1 in order to support arbitrary data
 * lengths. However, this leaves benefits from DMA underutilised. It is possible
 * to increase RX buffer size if packets are always a multiple of x bytes (e.g.
 * HAL_USART_DMA_RX_BUFFER_SIZE == 5: packet lengths can be 5, 10, 15, 20...).
 */
#if !defined(HAL_USART_DMA_RX_BUFFER_SIZE)
#define HAL_USART_DMA_RX_BUFFER_SIZE (1U)
#endif  // !defined(HAL_USART_DMA_RX_BUFFER_SIZE)

#if (HAL_USART_DMA_RX_BUFFER_SIZE > UARTE_DMA_RX_MAXCNT_MAXCNT_Max)
// cppcheck-suppress preprocessorErrorDirective
#error "RX buffer size cannot be larger than UARTE_DMA_RX_MAXCNT_MAXCNT_Max"
#endif  // (HAL_USART_DMA_RX_BUFFER_SIZE > UARTE_DMA_RX_MAXCNT_MAXCNT_Max)

/* Buffers used for reception */
static uint8_t            m_rx_buffer_1[HAL_USART_DMA_RX_BUFFER_SIZE];
static uint8_t            m_rx_buffer_2[HAL_USART_DMA_RX_BUFFER_SIZE];
static volatile uint8_t * m_active_rx_buffer;

/* UARTE can receive up to 4 bytes of data after stopping reception so a small
 * flush buffer is needed. */
#define RX_FLUSH_BUFFER_SIZE (5U)
static uint8_t       m_rx_flush_buffer[RX_FLUSH_BUFFER_SIZE];
static volatile bool m_rx_flush_started;

/** Indicate if USART is enabled */
static volatile uint32_t m_enabled;

/** Indicate if RX is enabled */
static bool m_rx_enabled;

#if defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)
/** Enable or disable HW flow control */
static void set_flow_control(bool hw);
#endif

/** Set uarte baudrate */
static bool set_baud(uint32_t baudrate);

/** Initialize DMA part */
static void init_dma(uint32_t baudrate);

/** Called each time a transfer is ready or when a previous transfer
 *  was finished. This function must be called with interrupts
 *  disabled or from interrupt context
 */
static void start_tx_lock(void);

#pragma GCC push_options
#pragma GCC target("general-regs-only")
/** Declare the interrupt handler */
void __attribute__((__interrupt__)) UARTE0_IRQHandler(void);
#pragma GCC push_options
#pragma GCC target("general-regs-only")


/**
 * \brief   Get the amount of data in RX flush buffer.
 * \return  Number of bytes in the buffer.
 */
static inline uint32_t get_rx_flush_buffer_amount(uint32_t amount)
{
    /* There is a HW bug which results in RX amount to not be updated
     * during flush if FIFO is empty. However, RXSTARTED event is set only if
     * there is data in FIFO:
     * - RXSTARTED == 1 -> number of bytes indicated by RX amount was flushed
     * - RXSTARTED == 0 -> no data was flushed
     *
     * This workaround is outlined in Nordic errata "[7] UARTE: RXD.AMOUNT
     * is not updated after RXFLUSH if FIFO is empty":
     * https://docs.nordicsemi.com/bundle/errata_nRF54L15_EngB/page/ERR/nRF54L15/EngineeringB/latest/anomaly_L15_7.html
     */

    uint32_t flush_buffer_amount;
    if (NRF_UARTE0_EVENTS_RXSTARTED == 1)
    {
        flush_buffer_amount = amount;
    }
    else
    {
        flush_buffer_amount = 0;
    }

    return flush_buffer_amount;
}


/**
 * \brief   Set next RX buffer for DMA transfer.
 */
static inline void set_next_rx_buffer(void)
{
    m_active_rx_buffer = (uint8_t *) NRF_UARTE0_RX_PTR;
    NRF_UARTE0_RX_PTR
        = ((m_active_rx_buffer == m_rx_buffer_1) ? (uint32_t) m_rx_buffer_2
                                                 : (uint32_t) m_rx_buffer_1);

    NRF_UARTE0_RX_MAXCNT = HAL_USART_DMA_RX_BUFFER_SIZE;
}


/**
 * \brief   Start RX flush.
 */
static inline void start_rx_flush(void)
{
    m_rx_flush_started   = true;
    NRF_UARTE0_RX_PTR    = (uint32_t) m_rx_flush_buffer;
    NRF_UARTE0_RX_MAXCNT = RX_FLUSH_BUFFER_SIZE;

    /* RXSTARTED must be cleared before triggering flush so that a
     * workaround for HW bug functions correctly. The workaround is
     * described in more detail in get_rx_flush_buffer_amount. */
    NRF_UARTE0_EVENTS_RXSTARTED = 0;

    NRF_UARTE0->TASKS_FLUSHRX = 1;
}


/**
 * \brief   Process received data from RX buffer.
 */
static inline void process_rx_buffer(void)
{
    uint8_t * active_buffer
        = (m_rx_flush_started ? m_rx_flush_buffer
                              : (uint8_t *) m_active_rx_buffer);

    uint32_t amount
        = (m_rx_flush_started ? get_rx_flush_buffer_amount(NRF_UARTE0_RX_AMOUNT)
                              : NRF_UARTE0_RX_AMOUNT);

    if (!m_rx_flush_started)
    {
        NRF_UARTE0_TASKS_STARTRX = 1;
    }

    if (m_rx_callback && (amount > 0))
    {
        m_rx_callback(active_buffer, amount);
    }
}


bool Usart_init(uint32_t baudrate, uart_flow_control_e flow_control)
{
    bool ret;

    /* Module variables */
    m_enabled     = 0;
    m_rx_enabled  = false;
    m_rx_callback = NULL;

    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Disabled;

    /* GPIO init */
    nrf_gpio_cfg_default(BOARD_USART_TX_PIN);
    nrf_gpio_pin_set(BOARD_USART_TX_PIN);
    nrf_gpio_cfg_default(BOARD_USART_RX_PIN);
    nrf_gpio_pin_set(BOARD_USART_RX_PIN);

    NRF_UARTE0->PSEL.TXD    = BOARD_USART_TX_PIN;
    NRF_UARTE0->PSEL.RXD    = BOARD_USART_RX_PIN;
    NRF_UARTE0_TASKS_STOPTX = 1;
    NRF_UARTE0_TASKS_STOPRX = 1;

#if defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)
    nrf_gpio_cfg_default(BOARD_USART_CTS_PIN);
    nrf_gpio_pin_set(BOARD_USART_CTS_PIN);
    nrf_gpio_cfg_default(BOARD_USART_RTS_PIN);
    nrf_gpio_pin_set(BOARD_USART_RTS_PIN);
    /* Set flow control */
    set_flow_control(flow_control == UART_FLOW_CONTROL_HW);
#endif  // defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)

    /* Uart speed */
    ret = set_baud(baudrate);
    /* Even if ret is False, do the end of init to have a uart at default
     * baudrate */

    /* Initialize DMA part*/
    init_dma(baudrate);

    /* APP IRQ */
    Sys_clearFastAppIrq(UART0_IRQn);
    Sys_enableFastAppIrq(UART0_IRQn,
                         APP_LIB_SYSTEM_IRQ_PRIO_HI,
                         UARTE0_IRQHandler);

    return ret;
}

void Usart_setEnabled(bool enabled)
{
    Sys_enterCriticalSection();
    if (enabled)
    {
        if (m_enabled == 0)
        {
            // Disable deep sleep
            DS_Disable(DS_SOURCE_USART);
            // Set output
            nrf_gpio_cfg(BOARD_USART_TX_PIN,
                         NRF_GPIO_PIN_DIR_OUTPUT,
                         NRF_GPIO_PIN_INPUT_CONNECT,
                         NRF_GPIO_PIN_NOPULL,
                         NRF_GPIO_PIN_S0S1,
                         NRF_GPIO_PIN_NOSENSE);

            NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
        }

        m_enabled++;
    }
    else
    {
        if (m_enabled > 0)
        {
            m_enabled--;
        }

        if (m_enabled == 0)
        {
            NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Disabled;
            // Set input
            nrf_gpio_cfg_input(BOARD_USART_TX_PIN, NRF_GPIO_PIN_NOPULL);
            // Enable deep sleep
            DS_Enable(DS_SOURCE_USART);
        }
    }

    Sys_exitCriticalSection();
}

void Usart_receiverOn(void)
{
    Sys_enterCriticalSection();
    if (m_rx_enabled)
    {
        // Already enabled
        Sys_exitCriticalSection();
        return;
    }

    m_rx_enabled       = true;
    m_rx_flush_started = false;

    // Clear events
    NRF_UARTE0->EVENTS_RXDRDY   = 0;
    NRF_UARTE0_EVENTS_ENDRX     = 0;
    NRF_UARTE0_EVENTS_RXSTARTED = 0;

    // Prepare RX buffer
    NRF_UARTE0_RX_PTR    = (uint32_t) m_rx_buffer_1;
    NRF_UARTE0_RX_MAXCNT = HAL_USART_DMA_RX_BUFFER_SIZE;
    m_active_rx_buffer   = m_rx_buffer_1;

    // Start uart reception
    NRF_UARTE0_TASKS_STARTRX = 1;
    Sys_exitCriticalSection();
}

void Usart_receiverOff(void)
{
    Sys_enterCriticalSection();

    // Stop uart reception
    NRF_UARTE0_TASKS_STOPRX = 1;

    m_rx_enabled = false;
    Sys_exitCriticalSection();
}

bool Usart_setFlowControl(uart_flow_control_e flow)
{
    bool ret = false;

#if defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)
    Sys_enterCriticalSection();
    if (m_enabled == 0)
    {
        switch (flow)
        {
            case UART_FLOW_CONTROL_NONE:
                set_flow_control(false);
                ret = true;
                break;
            case UART_FLOW_CONTROL_HW:
                set_flow_control(true);
                ret = true;
                break;
            default:
                break;
        }
    }
    Sys_exitCriticalSection();
#endif  // defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)

    return ret;
}

uint32_t Usart_sendBuffer(const void * buffer, uint32_t length)
{
    Sys_enterCriticalSection();

    // Check if there is enough room
    if (BUFFER_SIZE - DoubleBuffer_getIndex(m_tx_buffers) < length)
    {
        Sys_exitCriticalSection();
        return 0;
    }

    // Copy data to current buffer
    memcpy(DoubleBuffer_getActive(m_tx_buffers)
               + DoubleBuffer_getIndex(m_tx_buffers),
           buffer,
           length);

    DoubleBuffer_incrIndex(m_tx_buffers, length);

    start_tx_lock();

    Sys_exitCriticalSection();

    return length;
}

void Usart_enableReceiver(serial_rx_callback_f rx_callback)
{
    Sys_enterCriticalSection();
    /* Set callback */
    m_rx_callback = rx_callback;
    if (m_rx_callback)
    {
        // Enable RX input
        nrf_gpio_cfg(BOARD_USART_RX_PIN,
                     NRF_GPIO_PIN_DIR_INPUT,
                     NRF_GPIO_PIN_INPUT_CONNECT,
                     NRF_GPIO_PIN_NOPULL,
                     NRF_GPIO_PIN_S0S1,
                     NRF_GPIO_PIN_SENSE_LOW);
    }
    else
    {
        // Disable RX input: note autopowering uart will not work either
        nrf_gpio_cfg_default(BOARD_USART_RX_PIN);
    }
    Sys_exitCriticalSection();
}

uint32_t Usart_getMTUSize(void)
{
    return BUFFER_SIZE;
}

void Usart_flush(void)
{
    volatile uint32_t timeout = 20000;
    while (m_tx_ongoing && timeout > 0)
    {
        timeout--;
    }
}

#pragma GCC push_options
#pragma GCC target("general-regs-only")
/**
 * \brief Function for handling the USART Interrupt for TX End
 */
void __attribute__((__interrupt__)) UARTE0_IRQHandler(void)
{
    /* RX PTR is double-buffered so the next RX buffer can be set immediately
     * after RXSTARTED event has been generated. */
    if (NRF_UARTE0_EVENTS_RXSTARTED)
    {
        NRF_UARTE0_EVENTS_RXSTARTED = 0;
        set_next_rx_buffer();
    }

    if (NRF_UARTE0_EVENTS_ENDRX)
    {
        NRF_UARTE0_EVENTS_ENDRX = 0;
        process_rx_buffer();
    }

    if (NRF_UARTE0->EVENTS_RXTO)
    {
        NRF_UARTE0->EVENTS_RXTO = 0;
        start_rx_flush();
    }

    /* TX transfer complete, start next */
    if (NRF_UARTE0_EVENTS_ENDTX != 0)
    {
        m_tx_ongoing            = false;
        NRF_UARTE0_EVENTS_ENDTX = 0;
        NRF_UARTE0_TASKS_STOPTX = 1;

        // Retry to send in case something was queued while transmitting
        start_tx_lock();
    }

    /* Handle errors: Nothing to do specific at the moment */
    if (NRF_UARTE0->EVENTS_ERROR != 0)
    {
        NRF_UARTE0->EVENTS_ERROR = 0;
    }

    // read any event from peripheral to flush the write buffer:
    EVENT_READBACK = NRF_UARTE0->EVENTS_RXDRDY;
}
#pragma GCC pop_options

static void start_tx_lock(void)
{
    if (m_tx_ongoing)
    {
        // Already tx ongoing, this buffer will be chained later
        // as this function will be called again after current transfert
        return;
    }

    if (DoubleBuffer_getIndex(m_tx_buffers) == 0)
    {
        // Nothing to send
        return;
    }

    // No tx ongoing and something to send
    m_tx_ongoing = true;

    Usart_setEnabled(true);

    // Start DMA
    NRF_UARTE0_TX_PTR    = (uint32_t) DoubleBuffer_getActive(m_tx_buffers);
    NRF_UARTE0_TX_MAXCNT = DoubleBuffer_getIndex(m_tx_buffers);

    NRF_UARTE0_EVENTS_ENDTX  = 0;
    NRF_UARTE0_TASKS_STARTTX = 1;

    // Swipe buffers (it automatically reset writing index)
    DoubleBuffer_swipe(m_tx_buffers);
}

#if defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)
static void set_flow_control(bool hw)
{
    if (hw)
    {
        // Set input & pull down
        nrf_gpio_cfg_sense_input(BOARD_USART_CTS_PIN,
                                 NRF_GPIO_PIN_PULLDOWN,
                                 NRF_GPIO_PIN_NOSENSE);
        nrf_gpio_cfg_sense_input(BOARD_USART_RTS_PIN,
                                 NRF_GPIO_PIN_PULLDOWN,
                                 NRF_GPIO_PIN_NOSENSE);

        NRF_UARTE0->PSEL.RTS = 0xFFFFFFFF;
        NRF_UARTE0->PSEL.CTS = BOARD_USART_CTS_PIN;
        /* No parity, HW flow control */
        NRF_UARTE0->CONFIG = UARTE_CONFIG_HWFC_Enabled << UARTE_CONFIG_HWFC_Pos;
    }
    else
    {
        NRF_UARTE0->PSEL.RTS = 0xFFFFFFFF;
        NRF_UARTE0->PSEL.CTS = 0xFFFFFFFF;
        /* No parity, no HW flow control */
        NRF_UARTE0->CONFIG = 0;

        // Deactivate CTS & RTS pins
        nrf_gpio_cfg_default(BOARD_USART_CTS_PIN);
        nrf_gpio_cfg_default(BOARD_USART_RTS_PIN);
    }
}
#endif  // defined(BOARD_USART_CTS_PIN) && defined(BOARD_USART_RTS_PIN)

static bool set_baud(uint32_t baudrate)
{
    bool ret = true;
    switch (baudrate)
    {
        case 115200:
            NRF_UARTE0->BAUDRATE
                = (uint32_t) UARTE_BAUDRATE_BAUDRATE_Baud115200;

            break;
        case 125000:
            // This value is not from official nRF headers
            NRF_UARTE0->BAUDRATE = (uint32_t) (0x02000000UL);
            break;
        case 250000:
            NRF_UARTE0->BAUDRATE
                = (uint32_t) UARTE_BAUDRATE_BAUDRATE_Baud250000;

            break;
        case 460800:
            NRF_UARTE0->BAUDRATE
                = (uint32_t) UARTE_BAUDRATE_BAUDRATE_Baud460800;

            break;
        case 1000000:
            NRF_UARTE0->BAUDRATE = (uint32_t) UARTE_BAUDRATE_BAUDRATE_Baud1M;
            break;
        default:
            /* Intended baudrate is not in the list, default baudrate from chip
             * will be used */
            ret = false;
            break;
    }

    return ret;
}

static void init_dma(uint32_t baudrate)
{
    (void) baudrate;

    /* Interrupt init */
    NRF_UARTE0->INTENCLR = 0xffffffffUL;
    NRF_UARTE0->INTENSET = NRF_UARTE0_INTENSET;

    /* Configure TX part */
    DoubleBuffer_init(m_tx_buffers);
}

