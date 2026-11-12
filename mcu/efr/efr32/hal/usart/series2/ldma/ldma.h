/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * ldma.h
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    ldma.h
 * @brief   System-level LDMA controller initialization
 *
 * Manages the shared LDMA controller used by EUSART and other peripherals.
 *****************************************************************************/

#ifndef LDMA_H_
#define LDMA_H_

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <stdint.h>
#include <stdbool.h>


/******************************************************************************
 * TYPEDEFS
 *****************************************************************************/

/* Callback for EUSART RX channel completion (buffer full) */
typedef void (*efr32_ldma_eusart_rx_done_cb_t)(uint32_t channel_flags);
/******************************************************************************
 * PUBLIC API
 *****************************************************************************/

/**
 * @brief   Initialize the LDMA controller
 *
 * Enables the LDMA peripheral with default configuration.
 * Safe to call multiple times (idempotent).
 *
 * @return  true if successful, false otherwise
 */
bool efr32_ldma_init(void);

/**
 * @brief   Deinitialize the LDMA controller
 *
 * Disables LDMA peripheral. Stop all channels before calling.
 */
void efr32_ldma_deinit(void);

/**
 * @brief   Register callback for EUSART RX channel completion
 *
 * @param[in]  p_callback  Callback function to be invoked when RX transfer completes
 */
void efr32_ldma_register_eusart_rx_done_cb(
    efr32_ldma_eusart_rx_done_cb_t p_callback);

#endif /* LDMA_H_ */
