/* Copyright 2018 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "board.h"
#include "api.h"
#include "spi.h"
#include "spi_series.h"

#pragma GCC push_options
#pragma GCC target("general-regs-only")
/** Declare the interrupt handler */
void __attribute__((__interrupt__))     SPI_IRQHandler(void);
#pragma GCC pop_options

/** Is SPI module initialized */
static bool m_initialized = false;

/** Internal transfer description */
typedef struct
{
    spi_xfer_t *          client_xfer;  //< Transfer asked by client
    spi_on_transfer_done_cb_f   cb;     //< Callback to call at end of transfer
    bool                  free;         //< False if transfer ongoing
    volatile bool         done;         //< Is transfer done
} internal_xfer_desc;

/** Current tansfer ongoing. Only one transfer supported in this implementation */
static internal_xfer_desc m_current_xfer;

/**
 * \brief   Configure different SPI gpios (SCK, MOSI and MISO)
 * \param   conf_p
 *          Pointer to SPI configuration
 */
static void configure_gpios(const spi_conf_t * conf_p)
{
    // Configure clock pin (depending on mode)
    if ((conf_p->mode == SPI_MODE_LOW_FIRST)
        || (conf_p->mode == SPI_MODE_LOW_SECOND))
    {
        nrf_gpio_pin_set(BOARD_SPI_SCK_PIN);
    }
    else
    {
        nrf_gpio_pin_clear(BOARD_SPI_SCK_PIN);
    }

    nrf_gpio_cfg(BOARD_SPI_SCK_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 get_gpio_pin_drive_conf(conf_p->clock),
                 NRF_GPIO_PIN_NOSENSE);

    // Configure MOSI
    nrf_gpio_pin_clear(BOARD_SPI_MOSI_PIN);
    nrf_gpio_cfg(BOARD_SPI_MOSI_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 get_gpio_pin_drive_conf(conf_p->clock),
                 NRF_GPIO_PIN_NOSENSE);

    // Configure MISO
    nrf_gpio_cfg(BOARD_SPI_MISO_PIN,
                 NRF_GPIO_PIN_DIR_INPUT,
                 NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 get_gpio_pin_drive_conf(conf_p->clock),
                 NRF_GPIO_PIN_NOSENSE);
}

/**
 * \brief   Release different SPI gpios (SCK, MOSI and MISO)
 */
static void release_gpios()
{
    nrf_gpio_cfg_default(BOARD_SPI_SCK_PIN);
    nrf_gpio_cfg_default(BOARD_SPI_MOSI_PIN);
    nrf_gpio_cfg_default(BOARD_SPI_MISO_PIN);
}

/**
 * \brief   Set the SPI Mode of operation
 * \param   mode
 *          SPI mode of operation
 * \param   bit_order
 *          Bit order of SPI transfers
 * \return  True if successful, false otherwise
 */
static bool set_mode(spi_mode_e mode, spi_bit_order_e bit_order)
{
    nrf_spim_bit_order_t nrf_bit_order;
    switch (bit_order)
    {
        case SPI_ORDER_LSB:
            nrf_bit_order = NRF_SPIM_BIT_ORDER_LSB_FIRST;
            break;

        case SPI_ORDER_MSB:
            nrf_bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;
            break;

        default:
            // Invalid bit order
            return false;
    }

    nrf_spim_mode_t nrf_mode;
    switch(mode)
    {
        case SPI_MODE_LOW_FIRST:
            nrf_mode = NRF_SPIM_MODE_2;
            break;
        case SPI_MODE_LOW_SECOND:
            nrf_mode = NRF_SPIM_MODE_3;
            break;
        case SPI_MODE_HIGH_FIRST:
            nrf_mode = NRF_SPIM_MODE_0;
            break;
        case SPI_MODE_HIGH_SECOND:
            nrf_mode = NRF_SPIM_MODE_1;
            break;

        default:
            // Invalid SPI mode
            return false;
    }

    nrf_spim_configure(SPI_DEV, nrf_mode, nrf_bit_order);
    return true;
}


/**
 * \brief   Disable all SPI interrupts
 */
__attribute__((__always_inline__)) static inline void disable_all_interrupts()
{
    nrf_spim_int_disable(SPI_DEV,
                         NRF_SPIM_INT_STARTED_MASK | NRF_SPIM_INT_STOPPED_MASK
                             | NRF_SPIM_INT_ENDRX_MASK | NRF_SPIM_INT_ENDTX_MASK
                             | NRF_SPIM_INT_END_MASK);
}


spi_res_e SPI_init(spi_conf_t * conf_p)
{
    if (m_initialized)
    {
        return SPI_RES_ALREADY_INITIALIZED;
    }

    // Configure frequency
    if (!set_frequency(conf_p->clock))
    {
        return SPI_RES_INVALID_CONFIG;
    }

    // Configure mode
    if (!set_mode(conf_p->mode, conf_p->bit_order))
    {
        return SPI_RES_INVALID_CONFIG;
    }

    nrf_spim_orc_set(SPI_DEV, 0xFF);

    // Configure the gpios
    configure_gpios(conf_p);

    // Configure the SPIM module
    nrf_spim_pins_set(SPI_DEV,
                      BOARD_SPI_SCK_PIN,
                      BOARD_SPI_MOSI_PIN,
                      BOARD_SPI_MISO_PIN);

    disable_all_interrupts();

    // Enable SPI IRQ
    Sys_clearFastAppIrq(SPI_IRQn);
    Sys_enableFastAppIrq(SPI_IRQn,
                         APP_LIB_SYSTEM_IRQ_PRIO_HI,
                         SPI_IRQHandler);

    m_current_xfer.free = true;
    m_initialized = true;

    return SPI_RES_OK;
}

spi_res_e SPI_close()
{
    if (!m_initialized)
    {
        return SPI_RES_NOT_INITIALIZED;
    }

    m_initialized = false;

    // Disable SPIM module
    nrf_spim_disable(SPI_DEV);
    Sys_disableAppIrq(SPI_IRQn);

    disable_all_interrupts();

    // Set all gpios as default configuration
    release_gpios();

    return SPI_RES_OK;
}

spi_res_e SPI_transfer(spi_xfer_t * xfer_p,
                       spi_on_transfer_done_cb_f cb)
{
    if (!m_initialized)
    {
        return SPI_RES_NOT_INITIALIZED;
    }

    // Check if a transfer is already ongoing
    if (!m_current_xfer.free)
    {
        return SPI_RES_BUSY;
    }

    // Check transfer
    if (((xfer_p->write_ptr != NULL) && (xfer_p->write_size == 0)) ||
        ((xfer_p->write_ptr == NULL) && (xfer_p->write_size != 0)) ||
        ((xfer_p->read_ptr != NULL) && (xfer_p->read_size == 0)) ||
        ((xfer_p->read_ptr == NULL) && (xfer_p->read_size != 0)))
    {
        return SPI_RES_INVALID_XFER;
    }

    // Check transfer size
    if ((xfer_p->write_size > MAX_XFER_SIZE) || (xfer_p->read_size > MAX_XFER_SIZE))
    {
        return SPI_RES_INVALID_XFER;
    }


    // Enable SPIM module
    nrf_spim_enable(SPI_DEV);

    // Setup the transfer
    m_current_xfer.free = false;
    m_current_xfer.client_xfer = xfer_p;
    m_current_xfer.done = false;
    m_current_xfer.cb = cb;

    nrf_spim_tx_buffer_set(SPI_DEV, NULL, 0);
    nrf_spim_rx_buffer_set(SPI_DEV, NULL, 0);
    if (m_current_xfer.client_xfer->write_ptr != NULL)
    {
        nrf_spim_tx_buffer_set(SPI_DEV,
                               m_current_xfer.client_xfer->write_ptr,
                               m_current_xfer.client_xfer->write_size);
    }

    if (m_current_xfer.client_xfer->read_ptr != NULL)
    {
        nrf_spim_rx_buffer_set(SPI_DEV,
                               m_current_xfer.client_xfer->read_ptr,
                               m_current_xfer.client_xfer->read_size);
    }

    // Prepare the interrupt part
    disable_all_interrupts();
    nrf_spim_event_clear(SPI_DEV, NRF_SPIM_EVENT_END);

    // Enable interrupts we are interested
    nrf_spim_int_enable(SPI_DEV, NRF_SPIM_INT_END_MASK);

    // Start the transfer
    nrf_spim_task_trigger(SPI_DEV, NRF_SPIM_TASK_START);

    // Is it a blocking call
    if (m_current_xfer.cb == NULL)
    {
        // Active wait on end of transfer
        while (!m_current_xfer.done);
        m_current_xfer.free = true;
    }

    return SPI_RES_OK;
}

#pragma GCC push_options
#pragma GCC target("general-regs-only")
/**
 * \brief   Function to handle the SPI Interrupt.
 */
void __attribute__((__interrupt__)) SPI_IRQHandler(void)
{
    if (nrf_spim_event_check(SPI_DEV, NRF_SPIM_EVENT_END))
    {
        nrf_spim_event_clear(SPI_DEV, NRF_SPIM_EVENT_END);
        // Requested transfer is done
        m_current_xfer.done = true;

        // Disable SPIM module
        nrf_spim_disable(SPI_DEV);

        // If not blocking transfer, then call client cb
        if (m_current_xfer.cb != NULL)
        {
            // Free the transfer before calling cb to chain requests
            m_current_xfer.free = true;
            m_current_xfer.cb(SPI_RES_OK,
                              m_current_xfer.client_xfer);
        }
    }
}
#pragma GCC pop_options
