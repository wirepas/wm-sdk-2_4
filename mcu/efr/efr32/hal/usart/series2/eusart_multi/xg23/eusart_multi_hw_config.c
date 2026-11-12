/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * eusart_multi_hw_config.c
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    eusart_multi_hw_config.c
 * @brief   EFR32FG23 hardware configuration tables
 *
 * Defines the hardware configuration for EUSART instances on XG23.
 * The number of instances is determined by USART_MULTI_MAX_INSTANCES.
 *
 * Available EUSART peripherals on XG23:
 * - EUSART0: 0x4B010000 (TX IRQ 12, RX IRQ 11)
 * - EUSART1: 0x400A0000 (TX IRQ 14, RX IRQ 13)
 * - EUSART2: 0x400A4000 (TX IRQ 16, RX IRQ 15)
 *****************************************************************************/

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include "eusart_multi_internal.h"
#include "board.h"  /* For BOARD_USART_TX_PORT, BOARD_USART_TX_PIN, etc. */

/******************************************************************************
 * COMPILE-TIME VALIDATION
 *****************************************************************************/

#if USART_MULTI_MAX_INSTANCES > 3
#error "EFR32xG23 supports maximum 3 EUSART instances (EUSART0-EUSART2)"
#endif

/******************************************************************************
 * PIN CONFIGURATIONS
 *****************************************************************************/

/**
 * @brief   EUSART0 pin configuration
 *
 * Uses board-specific pin definitions from board.h
 */
static const eusart_pin_config_t m_eusart0_pins =
{
    .tx_port  = BOARD_USART_TX_PORT,
    .tx_pin   = BOARD_USART_TX_PIN,
    .rx_port  = BOARD_USART_RX_PORT,
    .rx_pin   = BOARD_USART_RX_PIN,
    .rts_port = 0xFF,  /* Unused */
    .rts_pin  = 0xFF,  /* Unused */
    .cts_port = 0xFF,  /* Unused */
    .cts_pin  = 0xFF,  /* Unused */
};

#if USART_MULTI_MAX_INSTANCES >= 2
/**
 * @brief   EUSART1 pin configuration
 */
static const eusart_pin_config_t m_eusart1_pins =
{
    .tx_port  = BOARD_EUSART1_TX_PORT,
    .tx_pin   = BOARD_EUSART1_TX_PIN,
    .rx_port  = BOARD_EUSART1_RX_PORT,
    .rx_pin   = BOARD_EUSART1_RX_PIN,
    .rts_port = 0xFF,  /* Unused */
    .rts_pin  = 0xFF,  /* Unused */
    .cts_port = 0xFF,  /* Unused */
    .cts_pin  = 0xFF,  /* Unused */
};
#endif /* USART_MULTI_MAX_INSTANCES >= 2 */

#if USART_MULTI_MAX_INSTANCES >= 3
/**
 * @brief   EUSART2 pin configuration
 *
 * Note: EUSART2 restricted to Port C/D only (DBUSCD architecture)
 */
static const eusart_pin_config_t m_eusart2_pins =
{
    .tx_port  = BOARD_EUSART2_TX_PORT,
    .tx_pin   = BOARD_EUSART2_TX_PIN,
    .rx_port  = BOARD_EUSART2_RX_PORT,
    .rx_pin   = BOARD_EUSART2_RX_PIN,
    .rts_port = 0xFF,  /* Unused */
    .rts_pin  = 0xFF,  /* Unused */
    .cts_port = 0xFF,  /* Unused */
    .cts_pin  = 0xFF,  /* Unused */
};
#endif /* USART_MULTI_MAX_INSTANCES >= 3 */

/******************************************************************************
 * HARDWARE CONFIGURATIONS
 *****************************************************************************/

/**
 * @brief   EUSART0 hardware configuration
 */
static const eusart_hw_config_t m_eusart0_hw_config =
{
    .p_eusart        = EUSART0,
    .ldma_tx_channel = 0,                    /* LDMA channel 0 for TX */
    .ldma_rx_channel = 1,                    /* LDMA channel 1 for RX */
    .tx_irqn         = EUSART0_TX_IRQn,     /* IRQ 12 */
    .rx_irqn         = EUSART0_RX_IRQn,     /* IRQ 11 */
};

#if USART_MULTI_MAX_INSTANCES >= 2
/**
 * @brief   EUSART1 hardware configuration
 */
static const eusart_hw_config_t m_eusart1_hw_config =
{
    .p_eusart        = EUSART1,
    .ldma_tx_channel = 2,                    /* LDMA channel 2 for TX */
    .ldma_rx_channel = 3,                    /* LDMA channel 3 for RX */
    .tx_irqn         = EUSART1_TX_IRQn,     /* IRQ 14 */
    .rx_irqn         = EUSART1_RX_IRQn,     /* IRQ 13 */
};
#endif /* USART_MULTI_MAX_INSTANCES >= 2 */

#if USART_MULTI_MAX_INSTANCES >= 3
/**
 * @brief   EUSART2 hardware configuration
 */
static const eusart_hw_config_t m_eusart2_hw_config =
{
    .p_eusart        = EUSART2,
    .ldma_tx_channel = 4,                    /* LDMA channel 4 for TX */
    .ldma_rx_channel = 5,                    /* LDMA channel 5 for RX */
    .tx_irqn         = EUSART2_TX_IRQn,     /* IRQ 16 */
    .rx_irqn         = EUSART2_RX_IRQn,     /* IRQ 15 */
};
#endif /* USART_MULTI_MAX_INSTANCES >= 3 */

/******************************************************************************
 * RUNTIME STATE (MUTABLE)
 *****************************************************************************/

/* Runtime state for each instance */
static eusart_state_t m_eusart0_state = {0};

#if USART_MULTI_MAX_INSTANCES >= 2
static eusart_state_t m_eusart1_state = {0};
#endif

#if USART_MULTI_MAX_INSTANCES >= 3
static eusart_state_t m_eusart2_state = {0};
#endif

/******************************************************************************
 * ERROR STATISTICS (MUTABLE)
 *****************************************************************************/

/* Error statistics for each instance */
static eusart_error_stats_t m_eusart0_error_stats = {0};

#if USART_MULTI_MAX_INSTANCES >= 2
static eusart_error_stats_t m_eusart1_error_stats = {0};
#endif

#if USART_MULTI_MAX_INSTANCES >= 3
static eusart_error_stats_t m_eusart2_error_stats = {0};
#endif

/******************************************************************************
 * CONFIGURATION ARRAY (PRIVATE)
 *****************************************************************************/

/**
 * @brief   Array of all EUSART configurations
 *
 * Private to this file. Access via efr32_usart_multi_get_config() only.
 * Array size is determined by USART_MULTI_MAX_INSTANCES.
 */
static const eusart_config_t m_eusart_configs[EUSART_TOTAL_INSTANCES] =
{
    /* EUSART0 - Always present */
    {
        .instance     = USART_MULTI_INSTANCE_0,
        .p_hw_config  = &m_eusart0_hw_config,
        .p_pin_config = &m_eusart0_pins,
        .p_state      = &m_eusart0_state,
        .p_error_stats = &m_eusart0_error_stats,
    },
#if USART_MULTI_MAX_INSTANCES >= 2
    /* EUSART1 - Only if configured */
    {
        .instance     = USART_MULTI_INSTANCE_1,
        .p_hw_config  = &m_eusart1_hw_config,
        .p_pin_config = &m_eusart1_pins,
        .p_state      = &m_eusart1_state,
        .p_error_stats = &m_eusart1_error_stats,
    },
#endif
#if USART_MULTI_MAX_INSTANCES >= 3
    /* EUSART2 - Only if configured */
    {
        .instance     = USART_MULTI_INSTANCE_2,
        .p_hw_config  = &m_eusart2_hw_config,
        .p_pin_config = &m_eusart2_pins,
        .p_state      = &m_eusart2_state,
        .p_error_stats = &m_eusart2_error_stats,
    },
#endif
};

/**
 * @brief   Number of EUSART instances configured
 */
static const uint8_t m_eusart_instance_count = EUSART_TOTAL_INSTANCES;

/******************************************************************************
 * PUBLIC API
 *****************************************************************************/

/**
 * @brief   Get complete EUSART configuration for instance
 *
 * @param   instance    EUSART instance
 *
 * @return  Pointer to eusart_config_t, or NULL if invalid instance
 */
const eusart_config_t * efr32_usart_multi_get_config(usart_multi_instance_id_t instance)
{
    if (instance >= m_eusart_instance_count)
    {
        return NULL;
    }

    return &m_eusart_configs[instance];
}
