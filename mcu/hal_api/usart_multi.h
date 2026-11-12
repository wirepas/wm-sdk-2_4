/*
 * usart_multi.h
 *
 *  Created on: 12.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    usart_multi.h
 * @brief   Multi-UART HAL API for concurrent multi-instance UART operations
 *          with DMA-based transfers, hardware flow control, idle detection,
 *          and low-power mode support
 ******************************************************************************/

#ifndef USART_MULTI_H_
#define USART_MULTI_H_

/******************************************************************************
 * Includes
 ******************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

/**
 * @brief Instance ID - identifies which UART peripheral
 */
typedef enum
{
    USART_MULTI_INSTANCE_0 = 0,  /**< UART instance 0 */
    USART_MULTI_INSTANCE_1 = 1,  /**< UART instance 1 */
    USART_MULTI_INSTANCE_2 = 2,  /**< UART instance 2 */
    USART_MULTI_INSTANCE_3 = 3,  /**< UART instance 3 */
    USART_MULTI_INSTANCE_4 = 4,  /**< UART instance 4 */
    USART_MULTI_INSTANCE_5 = 5,  /**< UART instance 5 */
    USART_MULTI_INSTANCE_6 = 6,  /**< UART instance 6 */
    USART_MULTI_INSTANCE_7 = 7,  /**< UART instance 7 */
    USART_MULTI_INSTANCE_8 = 8,  /**< UART instance 8 */
    USART_MULTI_INSTANCE_9 = 9   /**< UART instance 9 */
} usart_multi_instance_id_t;

/**
 * @brief Opaque instance type - driver owns memory, user holds pointer
 */
typedef void usart_multi_instance_t;

/**
 * @brief Flow control modes
 */
typedef enum
{
    USART_MULTI_FLOW_CONTROL_NONE = 0,  /**< No flow control (default) */
    USART_MULTI_FLOW_CONTROL_HW = 1     /**< Hardware RTS/CTS */
} usart_multi_flow_control_e;

/**
 * @brief Communication modes
 */
typedef enum
{
    USART_MULTI_MODE_ASYNC = 0,         /**< Asynchronous (default, all platforms) */
    USART_MULTI_MODE_SYNC_MASTER = 1,   /**< Synchronous master (USART/EUSART only) */
    USART_MULTI_MODE_SYNC_SLAVE = 2     /**< Synchronous slave (USART/EUSART only) */
} usart_multi_mode_e;

/**
 * @brief Power modes
 */
typedef enum
{
    USART_MULTI_POWER_MODE_NORMAL = 0,      /**< Normal power (default) */
    USART_MULTI_POWER_MODE_LOW_POWER = 1    /**< Low-power mode (EUSART: EM2) */
} usart_multi_power_mode_e;

/**
 * @brief Parity modes
 */
typedef enum
{
    USART_MULTI_PARITY_NONE = 0,    /**< No parity (default, 8N1) */
    USART_MULTI_PARITY_EVEN = 1,    /**< Even parity */
    USART_MULTI_PARITY_ODD = 2      /**< Odd parity */
} usart_multi_parity_e;

/**
 * @brief Stop bits
 */
typedef enum
{
    USART_MULTI_STOP_BITS_1 = 0,    /**< 1 stop bit (default, 8N1) */
    USART_MULTI_STOP_BITS_2 = 1     /**< 2 stop bits */
} usart_multi_stop_bits_e;

/**
 * @brief Error event flags - can be OR'd together
 */
typedef enum
{
    USART_MULTI_ERROR_FLAG_NONE = 0x00,     /**< No error */
    USART_MULTI_ERROR_FLAG_OVERRUN = 0x01,  /**< RX overrun (data lost) */
    USART_MULTI_ERROR_FLAG_FRAMING = 0x02,  /**< Invalid start/stop bits */
    USART_MULTI_ERROR_FLAG_PARITY = 0x04,   /**< Parity check failed */
    USART_MULTI_ERROR_FLAG_BREAK = 0x08     /**< Break condition */
} usart_multi_error_flags_e;

/**
 * @brief Status codes returned by API functions
 */
typedef enum
{
    USART_MULTI_SUCCESS = 0,                    /**< Operation successful */
    USART_MULTI_ERROR_INVALID_PARAM,            /**< Invalid parameter (NULL, out of range, etc.) */
    USART_MULTI_ERROR_NOT_INITIALIZED,          /**< Instance not initialized */
    USART_MULTI_ERROR_ALREADY_INITIALIZED,      /**< Instance already initialized */
    USART_MULTI_ERROR_HARDWARE,                 /**< Hardware failure */
    USART_MULTI_ERROR_BUSY,                     /**< Resource busy */
    USART_MULTI_ERROR_NOT_SUPPORTED,            /**< Feature not supported on platform */
    USART_MULTI_ERROR_TIMEOUT,                  /**< Operation timed out */
    USART_MULTI_ERROR_BUFFER_FULL,              /**< TX buffer full (no space) */
    USART_MULTI_ERROR_NOT_ENABLED               /**< UART peripheral not enabled */
} usart_multi_status_t;

/**
 * @brief Data format configuration
 */
typedef struct
{
    uint8_t                  data_bits;    /**< 7, 8 (default), or 9 data bits */
    usart_multi_parity_e     parity;       /**< Parity mode (0 = NONE, default) */
    usart_multi_stop_bits_e  stop_bits;    /**< Stop bits (0 = 1 bit, default) */
} usart_multi_data_format_t;

/**
 * @brief Initialization parameters for UART instance
 */
typedef struct
{
    uint32_t                       baudrate;            /**< Baud rate (9600-3000000) */
    usart_multi_flow_control_e     flow_control;        /**< Flow control (0 = NONE, default) */
    usart_multi_mode_e             mode;                /**< Communication mode (0 = ASYNC, default) */
    usart_multi_power_mode_e       power_mode;          /**< Power mode (0 = NORMAL, default) */
    usart_multi_data_format_t      data_format;         /**< Data format (default: 8N1) */
    bool                           dma_enabled;         /**< Enable DMA transfers */
    bool                           wakeup_enabled;      /**< RX wake from sleep (EUSART only) */
    bool                           idle_detect_enabled; /**< Enable idle line detection */
} usart_multi_init_params_t;

/**
 * @brief Error statistics counters
 */
typedef struct
{
    uint32_t overrun_errors;    /**< RX overrun count (data lost) */
    uint32_t framing_errors;    /**< Framing error count (baud mismatch) */
    uint32_t parity_errors;     /**< Parity error count */
    uint32_t break_errors;      /**< Break condition count */
} usart_multi_error_stats_t;

/**
 * @brief RX data callback - called when data received
 * @param instance_id UART instance that triggered callback
 * @param p_data Pointer to received data (read-only)
 * @param length Number of bytes received
 * @note Runs in ISR context
 */
typedef void (*usart_multi_rx_callback_f)(usart_multi_instance_id_t instance_id,
                                          const uint8_t *p_data,
                                          size_t length);

/**
 * @brief TX complete callback - called when TX finishes
 * @param instance_id UART instance that triggered callback
 * @note Runs in ISR context
 */
typedef void (*usart_multi_tx_callback_f)(usart_multi_instance_id_t instance_id);

/**
 * @brief Error callback - called on UART errors
 * @param instance_id UART instance that triggered callback
 * @param error_flags Bitmask of error flags (usart_multi_error_flags_e)
 * @note Runs in ISR context
 */
typedef void (*usart_multi_error_callback_f)(usart_multi_instance_id_t instance_id,
                                             uint32_t error_flags);

/**
 * @brief Idle line callback - called when RX line goes idle
 * @param instance_id UART instance that triggered callback
 * @param p_data Pointer to received data (read-only)
 * @param length Number of bytes received before idle
 * @note Runs in ISR context
 */
typedef void (*usart_multi_idle_callback_f)(usart_multi_instance_id_t instance_id,
                                            const uint8_t *p_data,
                                            size_t length);

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

/**
 * @brief   Initialize UART instance from driver's static pool
 * @details On success, populates *pp_instance with pointer to instance from
 *          driver's memory pool. UART starts disabled - call
 *          usart_multi_set_enabled(true) to activate.
 * @param   pp_instance Pointer to instance pointer (populated by driver)
 * @param   instance_id Instance ID to initialize (USART_MULTI_INSTANCE_0, etc.)
 * @param   p_params Initialization parameters
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Instance initialized successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer, invalid instance_id,
 *          or invalid parameters
 * @retval  USART_MULTI_ERROR_ALREADY_INITIALIZED Instance already initialized
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware initialization failed
 */
usart_multi_status_t usart_multi_init(usart_multi_instance_t **pp_instance,
                                      usart_multi_instance_id_t instance_id,
                                      const usart_multi_init_params_t *p_params);

/**
 * @brief   Deinitialize UART instance
 * @details Stops all transfers, disables peripheral, and sets *pp_instance to NULL
 * @param   pp_instance Pointer to instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Instance deinitialized successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or *pp_instance is NULL
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware deinitialization failed
 */
usart_multi_status_t usart_multi_deinit(usart_multi_instance_t **pp_instance);

/**
 * @brief   Enable or disable UART peripheral
 * @details Uses reference counting - multiple enables require matching disables
 * @param   p_instance Instance pointer
 * @param   enabled True to enable, false to disable
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS UART enabled/disabled successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware enable/disable failed
 */
usart_multi_status_t usart_multi_set_enabled(usart_multi_instance_t *p_instance,
                                             bool enabled);

/**
 * @brief   Change flow control mode
 * @param   p_instance Instance pointer
 * @param   flow_control Desired flow control mode
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Flow control configured successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or invalid value
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_ENABLED UART is enabled (must disable first)
 * @retval  USART_MULTI_ERROR_NOT_SUPPORTED Flow control not supported on platform
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware configuration failed
 */
usart_multi_status_t usart_multi_set_flow_control(usart_multi_instance_t *p_instance,
                                                  usart_multi_flow_control_e flow_control);

/**
 * @brief   Queue data for transmission
 * @details All-or-nothing: either all data queued (returns SUCCESS) or none.
 *          Non-blocking operation.
 * @param   p_instance Instance pointer
 * @param   p_buf Buffer to transmit (read-only)
 * @param   len Number of bytes to transmit
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS All data queued for transmission
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or len exceeds max TX size
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_ENABLED UART not enabled
 * @retval  USART_MULTI_ERROR_BUFFER_FULL TX buffer full, no space for this data
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware error during queuing
 */
usart_multi_status_t usart_multi_send_buffer(usart_multi_instance_t *p_instance,
                                             const void *p_buf,
                                             size_t len);

/**
 * @brief   Block until all pending TX data transmitted
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS All data transmitted successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_TIMEOUT Transmission did not complete in time
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware error during transmission
 */
usart_multi_status_t usart_multi_flush(usart_multi_instance_t *p_instance);

/**
 * @brief   Abort ongoing TX transfer
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Transmission aborted successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_SUPPORTED Abort not supported on this platform
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware abort failed
 */
usart_multi_status_t usart_multi_abort_transmit(usart_multi_instance_t *p_instance);

/**
 * @brief   Start receiver
 * @details RX callback must be registered first using
 *          usart_multi_register_rx_callback()
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Receiver started successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_ENABLED UART not enabled
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware RX enable failed
 */
usart_multi_status_t usart_multi_enable_receiver(usart_multi_instance_t *p_instance);

/**
 * @brief   Abort ongoing RX transfer and stop receiver
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Reception aborted successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_SUPPORTED Abort not supported on this platform
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware abort failed
 */
usart_multi_status_t usart_multi_abort_receive(usart_multi_instance_t *p_instance);

/**
 * @brief   Register RX data callback
 * @details Callback must be provided (cannot be NULL). Callback runs in ISR context.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or NULL callback
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_rx_callback(usart_multi_instance_t *p_instance,
                                                      usart_multi_rx_callback_f p_callback);

/**
 * @brief   Register TX complete callback
 * @details Pass NULL to disable. Callback runs in ISR context.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function (NULL to disable)
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_tx_callback(usart_multi_instance_t *p_instance,
                                                      usart_multi_tx_callback_f p_callback);

/**
 * @brief   Register error callback for error notification
 * @details Pass NULL to disable. Callback runs in ISR context.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function (NULL to disable)
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_error_callback(usart_multi_instance_t *p_instance,
                                                         usart_multi_error_callback_f p_callback);

/**
 * @brief   Register idle line detection callback
 * @details Pass NULL to disable. Requires idle_detect_enabled = true during init.
 *          Callback runs in ISR context. Used for variable-length packet detection.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function (NULL to disable)
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_idle_callback(usart_multi_instance_t *p_instance,
                                                        usart_multi_idle_callback_f p_callback);

/**
 * @brief   Get maximum bytes per usart_multi_send_buffer() call
 * @param   p_instance Instance pointer
 * @return  Maximum TX size in bytes, or 0 if instance is NULL or not initialized
 */
size_t usart_multi_get_max_tx_size(const usart_multi_instance_t *p_instance);

/**
 * @brief   Get total UART instances available on platform
 * @return  Number of available UART instances
 */
uint8_t usart_multi_get_instance_count(void);

/**
 * @brief   Retrieve accumulated error statistics
 * @param   p_instance Instance pointer
 * @param   p_stats Statistics structure to populate
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Statistics retrieved successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_get_error_stats(const usart_multi_instance_t *p_instance,
                                                 usart_multi_error_stats_t *p_stats);

/**
 * @brief   Reset all error counters to zero
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Error counters cleared successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_clear_error_stats(usart_multi_instance_t *p_instance);

#endif /* USART_MULTI_H_ */
