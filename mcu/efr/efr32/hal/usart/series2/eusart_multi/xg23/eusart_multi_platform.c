/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * eusart_multi_platform.c
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    eusart_multi_platform.c
 * @brief   EFR32FG23 platform-specific implementation
 *
 * Implements platform abstraction functions for XG23.
 * Supports 1-3 EUSART instances (configurable via USART_MULTI_MAX_INSTANCES).
 *****************************************************************************/

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include "eusart_multi_internal.h"

/******************************************************************************
 * PUBLIC FUNCTIONS
 *****************************************************************************/

/**
 * @brief   Get the maximum number of EUSART instances configured
 *
 * @return  Number of configured instances
 */
uint8_t efr32_usart_multi_platform_get_max_instances(void)
{
    return EUSART_TOTAL_INSTANCES;
}

/**
 * @brief   Check if instance supports low-power mode (EM2)
 *
 * @param   instance    Instance ID to check
 * @return  true if EM2 supported, false otherwise
 */
bool efr32_usart_multi_platform_supports_em2(usart_multi_instance_id_t instance)
{
    /* Only EUSART0 supports EM2 */
    return (instance == USART_MULTI_INSTANCE_0);
}

/**
 * @brief   Check if instance supports hardware flow control
 *
 * @param   instance    Instance ID to check
 * @return  true if supported, false otherwise
 */
bool efr32_usart_multi_platform_supports_flow_control(usart_multi_instance_id_t instance)
{
    /* Hardware flow control not implemented */
    (void)instance;
    return false;
}
