/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * eusart_multi_internal.h
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    eusart_multi_internal.h
 * @brief   EFR32 Series 2 EUSART driver internal header
 *
 * This file contains internal definitions, hardware configuration structures,
 * and function prototypes for the EFR32-specific EUSART driver implementation.
 *
 * All Silicon Labs SDK includes are contained in this header.
 *****************************************************************************/

#ifndef EUSART_MULTI_INTERNAL_H_
#define EUSART_MULTI_INTERNAL_H_

/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* HAL API */
#include "hal_api.h"
#include "usart_multi.h" /* Multi-UART HAL API types and callbacks */

/* LDMA controller */
#include "ldma.h"

/* Silicon Labs SDK */
#include "em_device.h"
#include "em_eusart.h"
#include "em_ldma.h"
#include "em_cmu.h"
#include "em_gpio.h"

/******************************************************************************
 * HARDWARE CONFIGURATION
 *****************************************************************************/

/* Baudrate validation limits */
#define EUSART_MIN_BAUDRATE 9600    /* Minimum supported baudrate */
#define EUSART_MAX_BAUDRATE 3000000 /* Maximum supported baudrate (3 Mbps) */

/*
 * Configurable number of EUSART instances
 *
 * Must be defined via config.mk: usart_multi_max_instances = N
 * which translates to -DUSART_MULTI_MAX_INSTANCES=N
 *
 * Valid range depends on the specific MCU.
 */
#ifndef USART_MULTI_MAX_INSTANCES
#error "USART_MULTI_MAX_INSTANCES must be defined in config.mk"
#endif

#if USART_MULTI_MAX_INSTANCES < 1
#error "USART_MULTI_MAX_INSTANCES must be at least 1"
#endif

/* Internal alias for array sizing */
#define EUSART_TOTAL_INSTANCES USART_MULTI_MAX_INSTANCES

/*
 * Buffer size configuration
 *
 * Fixed at 256 bytes.
 * This size works well with LDMA and provides good balance between
 * memory usage and transfer efficiency.
 */
#define BUFFER_SIZE                 256
#define EUSART_TX_BUFFER_SIZE_BYTES BUFFER_SIZE
#define EUSART_RX_BUFFER_SIZE_BYTES BUFFER_SIZE

/* Include double buffer management utilities */
#include "doublebuffer.h"

/******************************************************************************
 * CONFIGURATION TYPE DEFINITIONS
 *****************************************************************************/

/**
 * @brief   Pin configuration for one EUSART instance
 *
 * Board-specific pin assignments for TX, RX, RTS, CTS signals.
 * Use 0xFF for pins that are not connected on the board.
 */
typedef struct
{
    uint8_t tx_port;  /**< TX GPIO port */
    uint8_t tx_pin;   /**< TX GPIO pin number */
    uint8_t rx_port;  /**< RX GPIO port */
    uint8_t rx_pin;   /**< RX GPIO pin number */
    uint8_t rts_port; /**< RTS GPIO port */
    uint8_t rts_pin;  /**< RTS GPIO pin */
    uint8_t cts_port; /**< CTS GPIO port */
    uint8_t cts_pin;  /**< CTS GPIO pin */
} eusart_pin_config_t;

/**
 * @brief   Hardware configuration for one EUSART instance
 *
 * Contains all instance-specific hardware details from EFR32 datasheet.
 * These values are fixed by the chip architecture and never change.
 *
 * Reference: EFR32 Reference Manual
 * - Section: EUSART - Enhanced USART
 * - Section: LDMA - Linked DMA Controller
 */
typedef struct
{
    EUSART_TypeDef * p_eusart;        /**< EUSART peripheral base address */
    uint8_t          ldma_tx_channel; /**< LDMA channel for TX */
    uint8_t          ldma_rx_channel; /**< LDMA channel for RX */
    IRQn_Type        tx_irqn;         /**< TX complete IRQ number */
    IRQn_Type        rx_irqn;         /**< RX data available IRQ number */
} eusart_hw_config_t;

/**
 * @brief   EUSART driver state for one instance
 *
 * Tracks runtime state for a single EUSART instance.
 * Each EUSART has its own independent state.
 */
typedef struct
{
    /* Configuration */
    uint32_t                   baudrate;     /**< Current baud rate */
    usart_multi_flow_control_e flow_control; /**< Current flow control mode */
    bool    idle_detect_enabled;             /**< Idle line detection enabled */
    bool    initialized;                     /**< Initialization flag */
    bool    enabled;                         /**< Enable flag */
    bool    receiver_on;                     /**< Receiver state */
    uint8_t enable_count; /**< Reference count for enable/disable */

    /* TX double buffer (software-managed buffer swapping) */
    double_buffer_t   tx_buffers;    /**< TX double buffer structure */
    volatile bool     tx_ongoing;    /**< TX LDMA transfer in progress flag */
    LDMA_Descriptor_t tx_descriptor; /**< LDMA descriptor for TX transfers */

    /* RX double buffer (software-managed ping-pong for continuous reception) */
    double_buffer_t   rx_buffers;    /**< RX double buffer structure */
    volatile bool     rx_ongoing;    /**< RX LDMA transfer in progress flag */
    LDMA_Descriptor_t rx_descriptor; /**< LDMA descriptor for RX transfers */

    /*
     * Callbacks for usart_multi.h API
     *
     * All callbacks receive instance_id as first parameter and run in ISR
     * context.
     */
    usart_multi_rx_callback_f
        rx_callback; /**< RX data callback */
    usart_multi_tx_callback_f
        tx_callback; /**< TX complete callback */
    usart_multi_error_callback_f
        error_callback; /**< Error notification callback */
    usart_multi_idle_callback_f
        idle_callback; /**< Idle line detection callback */
} eusart_state_t;

/**
 * @brief   EUSART error statistics for one instance
 *
 * Tracks error counts for debugging and diagnostics.
 * Each EUSART has its own independent error counters.
 */
typedef struct
{
    uint32_t overrun_errors; /**< Overrun error count */
    uint32_t framing_errors; /**< Framing error count */
    uint32_t parity_errors;  /**< Parity error count */
    uint32_t break_errors;   /**< Break error count */
} eusart_error_stats_t;

/**
 * @brief   Complete EUSART configuration (HW + Pins + State)
 *
 * Combines hardware configuration with board-specific pin assignments
 * and runtime state. This is the main configuration structure passed
 * to all EFR32 EUSART driver functions.
 *
 * The configuration is obtained via eusart_get_config() and remains valid
 * throughout the lifetime of the driver.
 *
 * @note    Hardware and pin configs are const
 * @note    State and error_stats pointers point to per-instance mutable data
 */
typedef struct
{
    usart_multi_instance_id_t instance; /**< EUSART instance ID */
    const eusart_hw_config_t *
        p_hw_config; /**< Pointer to hardware configuration */
    const eusart_pin_config_t *
                     p_pin_config; /**< Pointer to pin configuration */
    eusart_state_t * p_state;      /**< Pointer to runtime state */
    eusart_error_stats_t *
        p_error_stats; /**< Pointer to error statistics */
} eusart_config_t;

/******************************************************************************
 * INTERNAL API - Configuration
 *****************************************************************************/
/**
 * @brief   Get complete EUSART configuration for instance
 *
 * Returns a pointer to the complete configuration (hardware + pins + state)
 * for the specified EUSART instance. The configuration is static and remains
 * valid for the lifetime of the program.
 *
 * @param   instance    EUSART instance
 *
 * @return  Pointer to eusart_config_t, or NULL if invalid instance
 */
const eusart_config_t * efr32_usart_multi_get_config(
    usart_multi_instance_id_t instance);

/******************************************************************************
 * INTERNAL API - Platform
 *****************************************************************************/

/**
 * @brief   Get the maximum number of EUSART instances configured
 *
 * @return  Number of configured instances
 */
uint8_t efr32_usart_multi_platform_get_max_instances(void);

/**
 * @brief   Check if instance supports low-power mode (EM2)
 *
 * @param   instance    Instance ID to check
 * @return  true if EM2 supported, false otherwise
 */
bool efr32_usart_multi_platform_supports_em2(
    usart_multi_instance_id_t instance);

/**
 * @brief   Check if instance supports hardware flow control
 *
 * @param   instance    Instance ID to check
 * @return  true if supported, false otherwise
 */
bool efr32_usart_multi_platform_supports_flow_control(
    usart_multi_instance_id_t instance);

/******************************************************************************
 * INTERNAL API - Interrupt Handlers
 *****************************************************************************/

/**
 * @brief   Handle LDMA RX channel completion (buffer full)
 * @param   flags   LDMA interrupt flags (all channels)
 */
void efr32_usart_multi_ldma_handle_rx_complete(uint32_t flags);

/******************************************************************************
 * INTERNAL API - ISR Registration
 *****************************************************************************/

/**
 * @brief   Register EUSART interrupt handlers
 * @param   p_config    Pointer to EUSART configuration
 * @return  true if successful, false otherwise
 */
bool efr32_usart_multi_register_isr(const eusart_config_t * p_config);

/**
 * @brief   Unregister EUSART interrupt handlers
 * @param   p_config    Pointer to EUSART configuration
 */
void efr32_usart_multi_unregister_isr(const eusart_config_t * p_config);

/******************************************************************************
 * INTERNAL API - LDMA Operations
 *****************************************************************************/

/**
 * @brief   Start TX LDMA transfer
 *
 * @param   p_config    Pointer to EUSART configuration
 * @param   p_data      Pointer to data buffer
 * @param   length      Number of bytes
 *
 * @return  true if started, false otherwise
 */
bool efr32_usart_multi_ldma_start_tx(const eusart_config_t * p_config,
                                     const uint8_t *         p_data,
                                     uint32_t                length);

/**
 * @brief   Start RX LDMA transfer
 *
 * Configures LDMA to transfer data from EUSART RXDATA to buffer.
 *
 * @param   p_config    Pointer to EUSART configuration
 *
 * @return  true if started, false otherwise
 */
bool efr32_usart_multi_ldma_start_rx(const eusart_config_t * p_config);

/**
 * @brief   Stop TX LDMA transfer
 *
 * @param   p_config    Pointer to EUSART configuration
 *
 * @return  Number of bytes remaining
 */
uint32_t efr32_usart_multi_ldma_stop_tx(const eusart_config_t * p_config);

/**
 * @brief   Stop RX LDMA and return bytes received
 *
 * @param   p_config    Pointer to EUSART configuration
 *
 * @return  Number of bytes received
 */
uint32_t efr32_usart_multi_ldma_stop_rx(const eusart_config_t * p_config);

#endif /* EUSART_MULTI_INTERNAL_H_ */
