/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * ldma.c
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    ldma.c
 * @brief   LDMA controller initialization and management
 *
 * Manages the global LDMA controller shared by all peripherals.
 * Uses reference counting and provides callback for EUSART RX completion.
 *****************************************************************************/

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include "ldma.h"

/* Silicon Labs SDK includes */
#include "em_device.h"  /* Device-specific definitions (LDMA_IF_ERROR, etc.) */
#include "em_ldma.h"
#include "em_cmu.h"
#include "em_bus.h"

/* Wirepas system includes */
#include "api.h"  /* Sys_enableFastAppIrq, Sys_clearFastAppIrq */

/******************************************************************************
 * STATIC DATA
 *****************************************************************************/

/* Reference count for LDMA users */
static uint8_t m_ldma_ref_count = 0;

/* Callback for EUSART RX channel completion */
static efr32_ldma_eusart_rx_done_cb_t mp_eusart_rx_done_cb = NULL;

/******************************************************************************
 * FORWARD DECLARATIONS
 *****************************************************************************/

/* LDMA interrupt handler  */
static void ldma_irq_handler(void);

/******************************************************************************
 * PUBLIC FUNCTIONS
 *****************************************************************************/

/**
 * @brief   Initialize the LDMA controller
 * @details Uses SDK's LDMA_Init() for configuration, then overrides NVIC IRQ
 *          registration with Wirepas IRQ system. Reference counted.
 * @return  true if successful, false otherwise
 */
bool efr32_ldma_init(void)
{
    LDMA_Init_t ldma_init;

    if (m_ldma_ref_count > 0)
    {
        m_ldma_ref_count++;
        return true;
    }

    /* Use SDK to initialize LDMA (handles clocks, peripheral, channels) */
    ldma_init = (LDMA_Init_t)LDMA_INIT_DEFAULT;
    LDMA_Init(&ldma_init);

    /* Override NVIC with Wirepas IRQ system */
    NVIC_DisableIRQ(LDMA_IRQn);
    NVIC_ClearPendingIRQ(LDMA_IRQn);

    Sys_clearFastAppIrq(LDMA_IRQn);
    Sys_enableFastAppIrq(LDMA_IRQn,
                         APP_LIB_SYSTEM_IRQ_PRIO_HI,
                         ldma_irq_handler);

    m_ldma_ref_count = 1;
    return true;
}

/**
 * @brief   Deinitialize the LDMA controller
 * @details Uses SDK's LDMA_DeInit() then cleans up Wirepas IRQ. Reference counted.
 */
void efr32_ldma_deinit(void)
{
    if (m_ldma_ref_count == 0)
    {
        return;
    }

    m_ldma_ref_count--;

    if (m_ldma_ref_count == 0)
    {
        /* Clear callback before deinit */
        mp_eusart_rx_done_cb = NULL;

        /* Unregister Wirepas IRQ handler */
        Sys_disableAppIrq(LDMA_IRQn);
        Sys_clearFastAppIrq(LDMA_IRQn);

        /* Use SDK to deinitialize LDMA (handles channels, peripheral, clocks) */
        LDMA_DeInit();
    }
}

/**
 * @brief   Register callback for EUSART RX channel completion
 * @param   p_callback  Callback function, or NULL to unregister
 */
void efr32_ldma_register_eusart_rx_done_cb(efr32_ldma_eusart_rx_done_cb_t p_callback)
{
    mp_eusart_rx_done_cb = p_callback;
}

/******************************************************************************
 * INTERRUPT HANDLERS
 *****************************************************************************/

#pragma GCC push_options
#pragma GCC target("general-regs-only")
/**
 * @brief   LDMA interrupt handler
 * @details Handles channel completion and error interrupts.
 *          Channel flags passed to callback for filtering.
 */
static void __attribute__((__interrupt__)) ldma_irq_handler(void)
{
    uint32_t flags;

    flags = LDMA_IntGetEnabled();
    LDMA_IntClear(flags);

    /* Pass all channel flags to callback for RX channel filtering */
    if (mp_eusart_rx_done_cb != NULL)
    {
        mp_eusart_rx_done_cb(flags);
    }

    /* Handle LDMA errors */
    if (flags & LDMA_IF_ERROR)
    {
        /* Error handling can be added here */
    }
}
#pragma GCC pop_options
