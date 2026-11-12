/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * eusart_multi_ints.c
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    eusart_multi_ints.c
 * @brief   EUSART interrupt handlers
 *
 * TX: LDMA-based transmission with completion interrupt.
 * RX: LDMA-based reception with RXTO for line idle detection.
  ******************************************************************************/

/******************************************************************************
 * INCLUDES
 ******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "eusart_multi_internal.h"
#include "api.h"

/******************************************************************************
 * DEFINES
 ******************************************************************************/

/**
 * @brief   Get instance ID from configuration structure
 * @param   p_config EUSART configuration
 * @return  Instance ID for callback invocation
 */
#define GET_INSTANCE_ID(p_config) ((p_config)->instance)

/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

/* Forward declarations for TX ISR wrappers (non-static, already declared in
 * system header) */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART0_TX_IRQHandler(void);
#if USART_MULTI_MAX_INSTANCES >= 2
void __attribute__((__interrupt__)) EUSART1_TX_IRQHandler(void);
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
void __attribute__((__interrupt__)) EUSART2_TX_IRQHandler(void);
#endif
#pragma GCC pop_options

/* Forward declarations for RX ISR wrappers (non-static, already declared in
 * system header) */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART0_RX_IRQHandler(void);
#if USART_MULTI_MAX_INSTANCES >= 2
void __attribute__((__interrupt__)) EUSART1_RX_IRQHandler(void);
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
void __attribute__((__interrupt__)) EUSART2_RX_IRQHandler(void);
#endif
#pragma GCC pop_options

/* TX ISR function pointer table indexed by EUSART instance */
static app_lib_system_irq_handler_f const
    m_eusart_tx_isr_table[USART_MULTI_MAX_INSTANCES]
    = { EUSART0_TX_IRQHandler
#if USART_MULTI_MAX_INSTANCES >= 2
        ,
        EUSART1_TX_IRQHandler
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
        ,
        EUSART2_TX_IRQHandler
#endif
      };

/* RX ISR function pointer table indexed by EUSART instance */
static app_lib_system_irq_handler_f const
    m_eusart_rx_isr_table[USART_MULTI_MAX_INSTANCES]
    = { EUSART0_RX_IRQHandler
#if USART_MULTI_MAX_INSTANCES >= 2
        ,
        EUSART1_RX_IRQHandler
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
        ,
        EUSART2_RX_IRQHandler
#endif
      };

/******************************************************************************
 * PRIVATE FUNCTIONS - Error Handling
 *****************************************************************************/

/** Error interrupt flags mask */
#define EUSART_ERROR_FLAGS (EUSART_IF_RXOF | EUSART_IF_FERR | EUSART_IF_PERR)

/**
 * @brief   Update error statistics from interrupt flags
 * @param   p_stats     Pointer to error statistics
 * @param   int_flags   Raw interrupt flags
 */
static void update_error_stats(eusart_error_stats_t * p_stats,
                               uint32_t               int_flags)
{
    if (int_flags & EUSART_IF_RXOF)
    {
        p_stats->overrun_errors++;
    }
    if (int_flags & EUSART_IF_FERR)
    {
        p_stats->framing_errors++;
    }
    if (int_flags & EUSART_IF_PERR)
    {
        p_stats->parity_errors++;
    }
}

/**
 * @brief   Convert hardware error flags to API error flags
 * @param   int_flags   Raw interrupt flags
 * @return  API error flags (USART_MULTI_ERROR_FLAG_*)
 */
static uint32_t convert_error_flags(uint32_t int_flags)
{
    uint32_t error_flags = 0;

    if (int_flags & EUSART_IF_RXOF)
    {
        error_flags |= USART_MULTI_ERROR_FLAG_OVERRUN;
    }
    if (int_flags & EUSART_IF_FERR)
    {
        error_flags |= USART_MULTI_ERROR_FLAG_FRAMING;
    }
    if (int_flags & EUSART_IF_PERR)
    {
        error_flags |= USART_MULTI_ERROR_FLAG_PARITY;
    }

    return error_flags;
}

/**
 * @brief   Handle EUSART error interrupts
 * @param   p_config    EUSART configuration
 * @param   int_flags   Raw interrupt flags
 */
static void handle_rx_errors(const eusart_config_t * p_config,
                             uint32_t                int_flags)
{
    eusart_state_t * p_state = p_config->p_state;
    uint32_t         error_flags;

    update_error_stats(p_config->p_error_stats, int_flags);

    if (p_state->error_callback == NULL)
    {
        return;
    }

    error_flags = convert_error_flags(int_flags);
    p_state->error_callback(p_config->instance, error_flags);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - TX Complete Handling
 *****************************************************************************/

/**
 * @brief   Handle TX complete interrupt (TXC)
 * @details Called when all data has been shifted out on the wire.
 *          Checks if more data is pending in the inactive buffer and chains
 *          the next transfer. Only calls the TX callback when all pending
 *          data has been transmitted.
 * @param   p_config    EUSART configuration
 */
static void handle_tx_complete(const eusart_config_t * p_config)
{
    EUSART_TypeDef * p_eusart = p_config->p_hw_config->p_eusart;
    eusart_state_t * p_state  = p_config->p_state;
    uint8_t *        p_next_buffer;
    uint32_t         next_length;

    /* Check if more data is waiting in the inactive buffer */
    next_length = DoubleBuffer_getIndex(p_state->tx_buffers);
    if (next_length > 0)
    {
        /* Swap buffers and chain next transfer */
        DoubleBuffer_swipe(p_state->tx_buffers);
        p_next_buffer = DoubleBuffer_getActive(p_state->tx_buffers);

        /* Clear TXC flag and start next transfer */
        EUSART_IntClear(p_eusart, EUSART_IF_TXC);

        if (efr32_usart_multi_ldma_start_tx(p_config, p_next_buffer, next_length))
        {
            /* Transfer chained successfully - TXC still enabled */
            return;
        }
        /* LDMA start failed - fall through to complete */
    }

    /* No more data or LDMA failed - transmission complete */
    EUSART_IntDisable(p_eusart, EUSART_IF_TXC);
    p_state->tx_ongoing = false;

    /* Swap buffer to prepare for next send_buffer() call */
    DoubleBuffer_swipe(p_state->tx_buffers);

    /* Notify application */
    if (p_state->tx_callback != NULL)
    {
        p_state->tx_callback(p_config->instance);
    }
}

/******************************************************************************
 * PRIVATE FUNCTIONS - RX Data Delivery (Common for RXTO and LDMA completion)
 *****************************************************************************/

/**
 * @brief   Handle RX data delivery (common for both RXTO and LDMA completion)
 * @details Stops LDMA, delivers data via callback, swaps buffers, restarts LDMA.
 *          Used by both RXTO (line idle) and LDMA completion (buffer full).
 * @param   p_config    EUSART configuration
 */
static void handle_rx_data_delivery(const eusart_config_t * p_config)
{
    eusart_state_t * p_state = p_config->p_state;
    uint8_t *        p_buffer;
    uint32_t         bytes_received;

    if (!p_state->receiver_on || p_state->rx_callback == NULL)
    {
        return;
    }

    /* Get buffer and stop LDMA to get byte count */
    p_buffer = DoubleBuffer_getActive(p_state->rx_buffers);
    bytes_received = efr32_usart_multi_ldma_stop_rx(p_config);

    if (bytes_received > 0)
    {
        /* Notify application */
        p_state->rx_callback(p_config->instance, p_buffer, bytes_received);

        /* Swap buffer for next reception */
        DoubleBuffer_swipe(p_state->rx_buffers);
    }

    /* Restart LDMA with new buffer */
    efr32_usart_multi_ldma_start_rx(p_config);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - RX Timeout Handling
 *****************************************************************************/

/**
 * @brief   Handle RX timeout interrupt (RXTO)
 * @details Called when RX line goes idle (partial or full packet received).
 * @param   p_config    EUSART configuration
 */
static void handle_rx_timeout(const eusart_config_t * p_config)
{
    /* RX timeout: line went idle, deliver data */
    handle_rx_data_delivery(p_config);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - Interrupt Handlers
 *****************************************************************************/

/**
 * @brief   EUSART TX interrupt handler
 * @param   p_config    EUSART configuration
 * @details Handles TXC (TX Complete) interrupt when all data shifted out.
 */
static void eusart_tx_interrupt_handler(const eusart_config_t * p_config)
{
    EUSART_TypeDef * p_eusart = p_config->p_hw_config->p_eusart;
    uint32_t         int_flags;

    int_flags = EUSART_IntGet(p_eusart);

    /* Handle TX Complete - all data shifted out */
    if (int_flags & EUSART_IF_TXC)
    {
        EUSART_IntClear(p_eusart, EUSART_IF_TXC);
        handle_tx_complete(p_config);
    }
}

/**
 * @brief   EUSART RX interrupt handler
 * @param   p_config    EUSART configuration
 * @details Handles RXTO (RX Timeout) and error interrupts.
 *          LDMA handles data transfer; RXTO signals end of packet.
 */
static void eusart_rx_interrupt_handler(const eusart_config_t * p_config)
{
    EUSART_TypeDef * p_eusart = p_config->p_hw_config->p_eusart;
    uint32_t         int_flags;

    int_flags = EUSART_IntGet(p_eusart);

    /* Handle error conditions */
    if (int_flags & EUSART_ERROR_FLAGS)
    {
        handle_rx_errors(p_config, int_flags);
        EUSART_IntClear(p_eusart, EUSART_ERROR_FLAGS);
    }

    /* Handle RX timeout - line went idle, packet complete */
    if (int_flags & EUSART_IF_RXTO)
    {
        EUSART_IntClear(p_eusart, EUSART_IF_RXTO);
        handle_rx_timeout(p_config);
    }
}

/******************************************************************************
 * PRIVATE ISR HANDLERS
 *****************************************************************************/

/*
 * @brief   EUSART0 TX interrupt service routine
 */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART0_TX_IRQHandler(void)
{
    eusart_tx_interrupt_handler(
        efr32_usart_multi_get_config(USART_MULTI_INSTANCE_0));
}
#pragma GCC pop_options

/*
 * @brief   EUSART0 RX interrupt service routine
 */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART0_RX_IRQHandler(void)
{
    eusart_rx_interrupt_handler(
        efr32_usart_multi_get_config(USART_MULTI_INSTANCE_0));
}
#pragma GCC pop_options

#if USART_MULTI_MAX_INSTANCES >= 2
/*
 * @brief   EUSART1 TX interrupt service routine
 */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART1_TX_IRQHandler(void)
{
    eusart_tx_interrupt_handler(
        efr32_usart_multi_get_config(USART_MULTI_INSTANCE_1));
}
#pragma GCC pop_options

/*
 * @brief   EUSART1 RX interrupt service routine
 */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART1_RX_IRQHandler(void)
{
    eusart_rx_interrupt_handler(
        efr32_usart_multi_get_config(USART_MULTI_INSTANCE_1));
}
#pragma GCC pop_options
#endif

#if USART_MULTI_MAX_INSTANCES >= 3
/*
 * @brief   EUSART2 TX interrupt service routine
 */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART2_TX_IRQHandler(void)
{
    eusart_tx_interrupt_handler(
        efr32_usart_multi_get_config(USART_MULTI_INSTANCE_2));
}
#pragma GCC pop_options

/*
 * @brief   EUSART2 RX interrupt service routine
 */
#pragma GCC push_options
#pragma GCC                         target("general-regs-only")
void __attribute__((__interrupt__)) EUSART2_RX_IRQHandler(void)
{
    eusart_rx_interrupt_handler(
        efr32_usart_multi_get_config(USART_MULTI_INSTANCE_2));
}
#pragma GCC pop_options
#endif

/******************************************************************************
 * PUBLIC FUNCTIONS
 ******************************************************************************/

/*
 * @brief   Register EUSART interrupt handlers in RAM vector table
 * @param   p_config    EUSART configuration
 * @return  true if successful, false otherwise
 */
bool efr32_usart_multi_register_isr(const eusart_config_t * p_config)
{
    const eusart_hw_config_t *   p_hw;
    usart_multi_instance_id_t    instance;
    app_lib_system_irq_handler_f tx_isr_handler;
    app_lib_system_irq_handler_f rx_isr_handler;

    if (p_config == NULL || p_config->p_hw_config == NULL)
    {
        return false;
    }

    p_hw     = p_config->p_hw_config;
    instance = p_config->instance;

    if (instance >= USART_MULTI_MAX_INSTANCES)
    {
        return false;
    }

    /* Select TX and RX ISR wrappers from tables */
    tx_isr_handler = m_eusart_tx_isr_table[instance];
    rx_isr_handler = m_eusart_rx_isr_table[instance];

    /* Clear any pending TX interrupt */
    Sys_clearFastAppIrq(p_hw->tx_irqn);

    /* Register TX interrupt handler */
    if (Sys_enableFastAppIrq(p_hw->tx_irqn,
                             APP_LIB_SYSTEM_IRQ_PRIO_HI,
                             tx_isr_handler)
        != APP_RES_OK)
    {
        return false;
    }

    /* Clear any pending RX interrupt */
    Sys_clearFastAppIrq(p_hw->rx_irqn);

    /* Register RX interrupt handler */
    if (Sys_enableFastAppIrq(p_hw->rx_irqn,
                             APP_LIB_SYSTEM_IRQ_PRIO_HI,
                             rx_isr_handler)
        != APP_RES_OK)
    {
        /* Cleanup TX handler on failure */
        Sys_disableAppIrq(p_hw->tx_irqn);
        Sys_clearFastAppIrq(p_hw->tx_irqn);
        return false;
    }

    return true;
}

/*
 * @brief   Unregister EUSART interrupt handlers
 * @param   p_config    EUSART configuration
 */
void efr32_usart_multi_unregister_isr(const eusart_config_t * p_config)
{
    const eusart_hw_config_t * p_hw;

    if (p_config == NULL || p_config->p_hw_config == NULL)
    {
        return;
    }

    p_hw = p_config->p_hw_config;

    /* Unregister TX interrupt handler */
    Sys_disableAppIrq(p_hw->tx_irqn);
    Sys_clearFastAppIrq(p_hw->tx_irqn);

    /* Unregister RX interrupt handler */
    Sys_disableAppIrq(p_hw->rx_irqn);
    Sys_clearFastAppIrq(p_hw->rx_irqn);
}

/**
 * @brief   Handle LDMA RX channel completion
 * @details Called from LDMA interrupt when RX buffer full.
 *          Delivers data to app, swaps buffer, restarts RX.
 * @param   flags   LDMA interrupt flags (all channels)
 */
void efr32_usart_multi_ldma_handle_rx_complete(uint32_t flags)
{
    const eusart_config_t * p_config;
    uint8_t                 instance;
    uint8_t                 max_instances;

    /* Check each configured instance's RX channel */
    max_instances = efr32_usart_multi_platform_get_max_instances();
    for (instance = 0; instance < max_instances; instance++)
    {
        p_config = efr32_usart_multi_get_config((usart_multi_instance_id_t)instance);
        if (p_config != NULL &&
            (flags & (1UL << p_config->p_hw_config->ldma_rx_channel)))
        {
            handle_rx_data_delivery(p_config);
        }
    }
}
