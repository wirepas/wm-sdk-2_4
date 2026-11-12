/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * eusart_multi_ldma.c
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    eusart_multi_ldma.c
 * @brief   EUSART LDMA transfer operations
 *
 * Implements LDMA transfer start/stop operations for EUSART TX and RX.
 * These functions configure and control LDMA channels for autonomous
 * data transfers between EUSART peripherals and memory buffers.
 *****************************************************************************/

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "eusart_multi_internal.h"

/******************************************************************************
 * PRIVATE FUNCTIONS
 *****************************************************************************/

/**
 * @brief Get LDMA peripheral signal for TX based on EUSART instance
 * @param instance EUSART instance ID
 * @return LDMA peripheral signal for TX FIFO level
 */
static LDMA_PeripheralSignal_t get_tx_peripheral_signal(
    usart_multi_instance_id_t instance)
{
    switch (instance)
    {
        case USART_MULTI_INSTANCE_0:
            return ldmaPeripheralSignal_EUSART0_TXFL;
#if USART_MULTI_MAX_INSTANCES >= 2
        case USART_MULTI_INSTANCE_1:
            return ldmaPeripheralSignal_EUSART1_TXFL;
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
        case USART_MULTI_INSTANCE_2:
            return ldmaPeripheralSignal_EUSART2_TXFL;
#endif
        default:
            return ldmaPeripheralSignal_EUSART0_TXFL;
    }
}

/**
 * @brief Get LDMA peripheral signal for RX based on EUSART instance
 * @param instance EUSART instance ID
 * @return LDMA peripheral signal for RX FIFO level
 */
static LDMA_PeripheralSignal_t get_rx_peripheral_signal(
    usart_multi_instance_id_t instance)
{
    switch (instance)
    {
        case USART_MULTI_INSTANCE_0:
            return ldmaPeripheralSignal_EUSART0_RXFL;
#if USART_MULTI_MAX_INSTANCES >= 2
        case USART_MULTI_INSTANCE_1:
            return ldmaPeripheralSignal_EUSART1_RXFL;
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
        case USART_MULTI_INSTANCE_2:
            return ldmaPeripheralSignal_EUSART2_RXFL;
#endif
        default:
            return ldmaPeripheralSignal_EUSART0_RXFL;
    }
}

/******************************************************************************
 * PUBLIC FUNCTIONS
 *****************************************************************************/

/**
 * @brief   Start TX LDMA transfer
 *
 * Configures and starts LDMA transfer from memory buffer to EUSART TXDATA.
 * Uses the LDMA channel assigned to this EUSART instance.
 *
 * @param   p_config    Pointer to EUSART configuration
 * @param   p_data      Pointer to data buffer to transmit
 * @param   length      Number of bytes to transfer
 *
 * @return  true if transfer started successfully, false otherwise
 */
bool efr32_usart_multi_ldma_start_tx(const eusart_config_t * p_config,
                                     const uint8_t *         p_data,
                                     uint32_t                length)
{
    eusart_state_t *             p_state;
    const eusart_hw_config_t *   p_hw;
    LDMA_TransferCfg_t           transfer_cfg;
    LDMA_PeripheralSignal_t      signal;

    if (p_config == NULL || p_data == NULL || length == 0)
    {
        return false;
    }

    p_state = p_config->p_state;
    p_hw    = p_config->p_hw_config;

    /* Get peripheral signal for this EUSART instance */
    signal = get_tx_peripheral_signal(p_config->instance);

    /* Configure transfer: peripheral request from EUSARTn TXFL */
    transfer_cfg = (LDMA_TransferCfg_t)LDMA_TRANSFER_CFG_PERIPHERAL(signal);

    /* Configure descriptor: memory to TXDATA, byte transfers */
    p_state->tx_descriptor = (LDMA_Descriptor_t)LDMA_DESCRIPTOR_SINGLE_M2P_BYTE(
        p_data,
        &p_hw->p_eusart->TXDATA,
        length);

    /* Start the transfer */
    LDMA_StartTransfer(p_hw->ldma_tx_channel,
                       &transfer_cfg,
                       &p_state->tx_descriptor);

    p_state->tx_ongoing = true;
    return true;
}

/**
 * @brief   Start RX LDMA transfer
 *
 * Configures and starts LDMA transfer from EUSART RXDATA to memory buffer.
 * Uses the LDMA channel assigned to this EUSART instance.
 *
 * @param   p_config    Pointer to EUSART configuration
 *
 * @return  true if transfer started successfully, false otherwise
 */
bool efr32_usart_multi_ldma_start_rx(const eusart_config_t * p_config)
{
    eusart_state_t *             p_state;
    const eusart_hw_config_t *   p_hw;
    LDMA_TransferCfg_t           transfer_cfg;
    LDMA_PeripheralSignal_t      signal;
    uint8_t *                    p_buffer;

    if (p_config == NULL)
    {
        return false;
    }

    p_state  = p_config->p_state;
    p_hw     = p_config->p_hw_config;
    p_buffer = DoubleBuffer_getActive(p_state->rx_buffers);

    /* Get peripheral signal for this EUSART instance */
    signal = get_rx_peripheral_signal(p_config->instance);

    /* Configure transfer: peripheral request from EUSARTn RXFL */
    transfer_cfg = (LDMA_TransferCfg_t)LDMA_TRANSFER_CFG_PERIPHERAL(signal);

    /* Configure descriptor: RXDATA to memory, byte transfers */
    p_state->rx_descriptor = (LDMA_Descriptor_t)LDMA_DESCRIPTOR_SINGLE_P2M_BYTE(
        &p_hw->p_eusart->RXDATA,
        p_buffer,
        BUFFER_SIZE);

    /* Start the transfer */
    LDMA_StartTransfer(p_hw->ldma_rx_channel,
                       &transfer_cfg,
                       &p_state->rx_descriptor);

    p_state->rx_ongoing = true;
    return true;
}

/**
 * @brief   Stop TX LDMA transfer
 *
 * Stops the ongoing TX LDMA transfer and returns the number of bytes
 * that were NOT transmitted (remaining in the transfer).
 *
 * @param   p_config    Pointer to EUSART configuration
 *
 * @return  Number of bytes remaining (not transmitted)
 */
uint32_t efr32_usart_multi_ldma_stop_tx(const eusart_config_t * p_config)
{
    const eusart_hw_config_t * p_hw;
    eusart_state_t *           p_state;
    uint32_t                   remaining;

    if (p_config == NULL || p_config->p_hw_config == NULL)
    {
        return 0;
    }

    p_hw    = p_config->p_hw_config;
    p_state = p_config->p_state;

    if (!p_state->tx_ongoing)
    {
        return 0;
    }

    /* Get remaining count before stopping */
    remaining = LDMA_TransferRemainingCount(p_hw->ldma_tx_channel);

    /* Stop the channel */
    LDMA_StopTransfer(p_hw->ldma_tx_channel);

    p_state->tx_ongoing = false;
    return remaining;
}

/**
 * @brief   Stop RX LDMA transfer and return bytes received
 *
 * Stops the ongoing RX LDMA transfer and returns the number of bytes
 * that were successfully received.
 *
 * @param   p_config    Pointer to EUSART configuration
 *
 * @return  Number of bytes received (for partial buffer handling)
 */
uint32_t efr32_usart_multi_ldma_stop_rx(const eusart_config_t * p_config)
{
    const eusart_hw_config_t * p_hw;
    eusart_state_t *           p_state;
    uint32_t                   remaining;
    uint32_t                   received;

    if (p_config == NULL || p_config->p_hw_config == NULL)
    {
        return 0;
    }

    p_hw    = p_config->p_hw_config;
    p_state = p_config->p_state;

    if (!p_state->rx_ongoing)
    {
        return 0;
    }

    /* Get remaining count before stopping */
    remaining = LDMA_TransferRemainingCount(p_hw->ldma_rx_channel);
    received  = BUFFER_SIZE - remaining;

    /* Stop the channel */
    LDMA_StopTransfer(p_hw->ldma_rx_channel);

    p_state->rx_ongoing = false;
    return received;
}
