/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * usart_compat.c
 *
 *  Created on: 24.02.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    usart_compat.c
 * @brief   Backward compatibility wrapper for legacy usart.h API
 *
 * This file implements the legacy single-instance USART HAL API (usart.h) by
 * wrapping the new multi-instance usart_multi.h API.
 *
 * Provides seamless backward compatibility for existing applications
 * that use the legacy usart.h API.
 *
 * Platform-agnostic
 *****************************************************************************/

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "usart.h"
#include "usart_multi.h"

/******************************************************************************
 * DEFINES
 *****************************************************************************/

/* Default UART instance to use for legacy API (0 = uart instance 0) */
#ifndef UART_INSTANCE
#define UART_INSTANCE 0
#endif

/* Validate UART instance number */
#if (UART_INSTANCE < 0) || (UART_INSTANCE >= USART_MULTI_MAX_INSTANCES)
// cppcheck-suppress preprocessorErrorDirective
#error "UART_INSTANCE must be valid for configured USART_MULTI_MAX_INSTANCES"
#endif

/******************************************************************************
 * STATIC DATA
 *****************************************************************************/

/* Cached pointer to UART instance */
static usart_multi_instance_t *mp_instance = NULL;

/* User's legacy RX callback (doesn't have instance_id parameter) */
static serial_rx_callback_f m_legacy_rx_callback = NULL;

/******************************************************************************
 * PRIVATE FUNCTIONS
 *****************************************************************************/

/*
 * @brief   Adapter callback that bridges usart_multi to legacy callback
 * @param   instance_id UART instance (ignored for legacy API)
 * @param   p_data      Pointer to received data
 * @param   length      Number of bytes received
 */
static void rx_callback_adapter(usart_multi_instance_id_t instance_id,
                                const uint8_t *p_data,
                                size_t length)
{
    (void)instance_id; /* Legacy API doesn't use instance_id */

    if (m_legacy_rx_callback != NULL)
    {
        /* Legacy callback takes non-const pointer - cast away const
         * (legacy code should not modify the buffer) */
        m_legacy_rx_callback((uint8_t *)p_data, length);
    }
}

/******************************************************************************
 * PUBLIC FUNCTIONS (Legacy USART API Implementation)
 *****************************************************************************/

/*
 * @brief   Initialize USART
 *
 * @param   baudrate        Desired baud rate in bps
 * @param   flow_control    Flow control mode
 *
 * @return  true if successful, false otherwise
 */
bool Usart_init(uint32_t baudrate, uart_flow_control_e flow_control)
{
    usart_multi_init_params_t params = {0};
    usart_multi_status_t status;

    /* Build init parameters */
    params.baudrate = baudrate;
    params.flow_control = (usart_multi_flow_control_e)flow_control;
    params.mode = USART_MULTI_MODE_ASYNC;
    params.power_mode = USART_MULTI_POWER_MODE_NORMAL;
    params.data_format.data_bits = 8;
    params.data_format.parity = USART_MULTI_PARITY_NONE;
    params.data_format.stop_bits = USART_MULTI_STOP_BITS_1;
    params.dma_enabled = true; /* Note DMA hardcoded to true here */
    params.wakeup_enabled = false;
    params.idle_detect_enabled = false;

    /* Initialize the instance */
    status = usart_multi_init(&mp_instance,
                              (usart_multi_instance_id_t)UART_INSTANCE,
                              &params);

    return (status == USART_MULTI_SUCCESS);
}

/*
 * @brief   Enable or disable USART
 *
 * @param   enabled     true to enable, false to disable
 */
void Usart_setEnabled(bool enabled)
{
    if (mp_instance == NULL)
    {
        return;
    }

    usart_multi_set_enabled(mp_instance, enabled);
}

/*
 * @brief   Enable USART receiver
 */
void Usart_receiverOn(void)
{
    if (mp_instance == NULL)
    {
        return;
    }

    usart_multi_enable_receiver(mp_instance);
}

/*
 * @brief   Disable USART receiver
 */
void Usart_receiverOff(void)
{
    if (mp_instance == NULL)
    {
        return;
    }

    usart_multi_abort_receive(mp_instance);
}

/*
 * @brief   Set USART flow control mode
 *
 * @param   flow    Flow control mode
 *
 * @return  true if successful, false otherwise
 */
bool Usart_setFlowControl(uart_flow_control_e flow)
{
    usart_multi_status_t status;

    if (mp_instance == NULL)
    {
        return false;
    }

    status = usart_multi_set_flow_control(mp_instance,
                                          (usart_multi_flow_control_e)flow);

    return (status == USART_MULTI_SUCCESS);
}

/*
 * @brief   Send buffer via USART
 *
 * @param   buf     Buffer to send
 * @param   len     Length of buffer
 *
 * @return  Number of bytes queued for transmission (0 or len)
 */
uint32_t Usart_sendBuffer(const void *buf, uint32_t len)
{
    usart_multi_status_t status;

    if (mp_instance == NULL)
    {
        return 0;
    }

    status = usart_multi_send_buffer(mp_instance, buf, len);

    /* Legacy API returns 0 on failure, len on success */
    return (status == USART_MULTI_SUCCESS) ? len : 0;
}

/*
 * @brief   Enable USART receiver with callback
 *
 * @param   callback    Callback function for received data (NULL to disable)
 */
void Usart_enableReceiver(serial_rx_callback_f callback)
{
    if (mp_instance == NULL)
    {
        return;
    }

    /* Store legacy callback for adapter */
    m_legacy_rx_callback = callback;

    if (callback != NULL)
    {
        /* Register our adapter callback */
        usart_multi_register_rx_callback(mp_instance, rx_callback_adapter);
    }
    else
    {
        /* NULL callback - just clear our stored callback
         * Note: usart_multi API doesn't allow NULL rx callback,
         * so we just stop calling the user's callback */
        m_legacy_rx_callback = NULL;
    }
}

/*
 * @brief   Get maximum transmission unit size
 *
 * @return  MTU size in bytes
 */
uint32_t Usart_getMTUSize(void)
{
    if (mp_instance == NULL)
    {
        return 0;
    }

    return (uint32_t)usart_multi_get_max_tx_size(mp_instance);
}

/*
 * @brief   Flush USART transmit buffer
 */
void Usart_flush(void)
{
    if (mp_instance == NULL)
    {
        return;
    }

    usart_multi_flush(mp_instance);
}
