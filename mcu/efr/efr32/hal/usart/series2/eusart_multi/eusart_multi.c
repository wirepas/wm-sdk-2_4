/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * eusart_multi.c
 *
 *  Created on: 29.01.2026
 *      Author: Wirepas Ltd
 */

/******************************************************************************
 * @file    eusart_multi.c
 * @brief   EUSART multi-instance driver implementation
 *
 * Main implementation file for the EUSART multi-instance driver.
 * Supports concurrent operation of multiple EUSART instances.
 *
 * TX: Non-blocking via LDMA, TXC interrupt signals completion.
 * RX: LDMA-based with RXTO (RX timeout) for partial packet detection.
 *****************************************************************************/
// CPD-OFF
/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <string.h>
#include "eusart_multi_internal.h"
#include "api.h" 

/******************************************************************************
 * TYPE CASTING MACROS
 *****************************************************************************/

/* Cast between opaque API type and internal config type */
#define INSTANCE_TO_CONFIG(p) ((const eusart_config_t *)(p))
#define CONFIG_TO_INSTANCE(p) ((usart_multi_instance_t *)(p))

/******************************************************************************
 * PRIVATE FUNCTIONS - Clock Management
 *****************************************************************************/

/**
 * @brief Get the CMU clock identifier for a given EUSART instance.
 *
 * @param[in] instance The EUSART instance identifier.
 *
 * @return The CMU clock type for the specified EUSART instance.
 */
static CMU_Clock_TypeDef get_eusart_clock(usart_multi_instance_id_t instance)
{
    /* Select clock based on instance */
    switch (instance)
    {
        case USART_MULTI_INSTANCE_0:
            return cmuClock_EUSART0;
        case USART_MULTI_INSTANCE_1:
            return cmuClock_EUSART1;
        case USART_MULTI_INSTANCE_2:
            return cmuClock_EUSART2;
        default:
            return cmuClock_EUSART0;
    }
}

/**
 * @brief Enable clocks for the EUSART peripheral.
 *
 * Enables the EUSART clock and configures the clock tree to use HFXO
 * as the clock source via EM01GRPCCLK.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void enable_clocks(const eusart_config_t * p_config)
{
    CMU_Clock_TypeDef eusart_clock = get_eusart_clock(p_config->instance);

    /* Enable GPIO clock for pin configuration */
    CMU_ClockEnable(cmuClock_GPIO, true);

    /* Enable and wait for HFXO oscillator */
    CMU_OscillatorEnable(cmuOsc_HFXO, true, true);

    /* Configure EM01GRPCCLK to use HFXO (shared by all EUSARTs) */
    CMU_ClockSelectSet(cmuClock_EM01GRPCCLK, cmuSelect_HFXO);


    if (p_config->instance == USART_MULTI_INSTANCE_0)
    {
        /* EUSART0 has dedicated clock select */
        CMU_ClockSelectSet(cmuClock_EUSART0CLK, cmuSelect_EM01GRPCCLK);
    }

    /* Enable peripheral clock gate */
    CMU_ClockEnable(eusart_clock, true);
}

/**
 * @brief Disable clocks for the EUSART peripheral.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void disable_clocks(const eusart_config_t * p_config)
{
    /* Disable EUSART peripheral clock */
    CMU_ClockEnable(get_eusart_clock(p_config->instance), false);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - EUSART Configuration
 *****************************************************************************/

/**
 * @brief Configure the EUSART peripheral with the specified settings.
 *
 * Initializes the EUSART hardware with the configured baudrate and oversampling,
 * and disables all interrupts.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void configure_eusart(const eusart_config_t * p_config)
{
    EUSART_TypeDef *        p_eusart = p_config->p_hw_config->p_eusart;
    const eusart_state_t *  p_state = p_config->p_state;

    /* Initialize with default HF settings */
    EUSART_UartInit_TypeDef init = EUSART_UART_INIT_DEFAULT_HF;

    /* Keep EUSART disabled during init */
    init.enable = eusartDisable;
    /* Set configured baudrate */
    init.baudrate = p_state->baudrate;
    /* Set 4x oversampling */
    init.oversampling = EUSART_CFG0_OVS_X4;
    /* Initialize EUSART hardware */
    EUSART_UartInitHf(p_eusart, &init);

    /* Configure RX timeout. RXTIMEOUT is used for partial packet detection when receiver is enabled. */
    p_eusart->CFG1 = (p_eusart->CFG1 & ~_EUSART_CFG1_RXTIMEOUT_MASK) |
                     EUSART_CFG1_RXTIMEOUT_TWOFRAMES;

    /* Disable all interrupts */
    EUSART_IntDisable(p_eusart, _EUSART_IF_MASK);
    /* Clear all interrupt flags */
    EUSART_IntClear(p_eusart, _EUSART_IF_MASK);
}

/**
 * @brief Reset the EUSART peripheral to its default state.
 *
 * Disables the EUSART and performs a hardware reset.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void reset_eusart(const eusart_config_t * p_config)
{
    /* Get EUSART hardware pointer */
    EUSART_TypeDef * p_eusart = p_config->p_hw_config->p_eusart;

    /* Disable EUSART */
    EUSART_Enable(p_eusart, eusartDisable);
    /* Reset EUSART to default state */
    EUSART_Reset(p_eusart);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - GPIO Routing
 *****************************************************************************/

/**
 * @brief Configure GPIO routing for the EUSART peripheral.
 *
 * Sets up the GPIO routing registers to connect the EUSART TX and RX signals
 * to the specified GPIO pins.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void configure_gpio_routing(const eusart_config_t * p_config)
{
    const eusart_pin_config_t * p_pins = p_config->p_pin_config;

    /* Enable TX and RX pin routing */
    GPIO->EUSARTROUTE[p_config->instance].ROUTEEN = GPIO_EUSART_ROUTEEN_TXPEN | GPIO_EUSART_ROUTEEN_RXPEN;
    
    /* Configure TX route */
    GPIO->EUSARTROUTE[p_config->instance].TXROUTE =
        /* Set TX port */
        (p_pins->tx_port << _GPIO_EUSART_TXROUTE_PORT_SHIFT) |
        /* Set TX pin */
        (p_pins->tx_pin << _GPIO_EUSART_TXROUTE_PIN_SHIFT);

    /* Configure RX route */
    GPIO->EUSARTROUTE[p_config->instance].RXROUTE =
        /* Set RX port */
        (p_pins->rx_port << _GPIO_EUSART_RXROUTE_PORT_SHIFT) |
        /* Set RX pin */
        (p_pins->rx_pin << _GPIO_EUSART_RXROUTE_PIN_SHIFT);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - Pin Mode Management
 *****************************************************************************/

/**
 * @brief Enable the TX pin for transmission.
 *
 * Configures the TX pin as a push-pull output.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void enable_tx_pin(const eusart_config_t * p_config)
{
    /* Get pin configuration */
    const eusart_pin_config_t * p_pins = p_config->p_pin_config;

    /* Set TX pin as push-pull output */
    hal_gpio_set_mode(p_pins->tx_port, p_pins->tx_pin, GPIO_MODE_OUT_PP);
}

/**
 * @brief Disable the TX pin.
 *
 * Configures the TX pin as an input with pull-up (DOUT selects pull direction).
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void disable_tx_pin(const eusart_config_t * p_config)
{
    /* Get pin configuration */
    const eusart_pin_config_t * p_pins = p_config->p_pin_config;

    /* Set TX pin as input with pull */
    hal_gpio_set_mode(p_pins->tx_port, p_pins->tx_pin, GPIO_MODE_IN_PULL);
    /* Select pull-up direction (DOUT=1 for pull-up, DOUT=0 for pull-down) */
    hal_gpio_set(p_pins->tx_port, p_pins->tx_pin);
}

/**
 * @brief Enable the RX pin for reception.
 *
 * Configures the RX pin as an input with pull-up (DOUT selects pull direction).
 * Pull-up keeps RX at idle high state and prevents floating input.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void enable_rx_pin(const eusart_config_t * p_config)
{
    /* Get pin configuration */
    const eusart_pin_config_t * p_pins = p_config->p_pin_config;

    /* Set RX pin as input with pull */
    hal_gpio_set_mode(p_pins->rx_port, p_pins->rx_pin, GPIO_MODE_IN_PULL);
    /* Select pull-up direction (DOUT=1 for pull-up, DOUT=0 for pull-down) */
    hal_gpio_set(p_pins->rx_port, p_pins->rx_pin);
}

/**
 * @brief Disable the RX pin.
 *
 * Configures the RX pin as disabled and clears DOUT register.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 */
static void disable_rx_pin(const eusart_config_t * p_config)
{
    /* Get pin configuration */
    const eusart_pin_config_t * p_pins = p_config->p_pin_config;

    /* Disable RX pin */
    hal_gpio_set_mode(p_pins->rx_port, p_pins->rx_pin, GPIO_MODE_DISABLED);
    /* Clear DOUT (mimics legacy driver, may have no effect in disabled mode) */
    hal_gpio_clear(p_pins->rx_port, p_pins->rx_pin);
}

/******************************************************************************
 * PRIVATE FUNCTIONS - Peripheral Enable/Disable
 *****************************************************************************/

/**
 * @brief Enable the EUSART peripheral hardware.
 *
 * Performs all hardware initialization: clocks, EUSART config, GPIO routing,
 * TX pin, ISR registration, and enables TX mode.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 *
 * @return USART_MULTI_SUCCESS on success, USART_MULTI_ERROR_HARDWARE on failure.
 */
static usart_multi_status_t enable_peripheral(const eusart_config_t * p_config)
{
    EUSART_TypeDef * p_eusart = p_config->p_hw_config->p_eusart;

    /* Initialize LDMA controller */
    if (!efr32_ldma_init())
    {
        return USART_MULTI_ERROR_HARDWARE;
    }

    /* Register LDMA RX completion callback for continuous streaming */
    efr32_ldma_register_eusart_rx_done_cb(efr32_usart_multi_ldma_handle_rx_complete);

    /* Enable clocks for register access */
    enable_clocks(p_config);

    /* Configure EUSART registers */
    configure_eusart(p_config);

    /* Configure GPIO routing */
    configure_gpio_routing(p_config);

    /* Enable TX pin */
    enable_tx_pin(p_config);

    /* Register interrupt handlers */
    if (!efr32_usart_multi_register_isr(p_config))
    {
        disable_tx_pin(p_config);
        reset_eusart(p_config);
        disable_clocks(p_config);
        efr32_ldma_deinit();
        return USART_MULTI_ERROR_HARDWARE;
    }

    /* Enable EUSART in TX-only mode */
    EUSART_Enable(p_eusart, eusartEnableTx);

    return USART_MULTI_SUCCESS;
}

/**
 * @brief Disable the EUSART peripheral hardware.
 *
 * Tears down hardware: disables receiver if active, unregisters ISRs,
 * disables TX pin, resets peripheral, and disables clocks.
 *
 * @param[in] p_config Pointer to the EUSART configuration structure.
 * @param[in,out] p_state Pointer to the EUSART state structure.
 */
static void disable_peripheral(const eusart_config_t * p_config,
                               eusart_state_t * p_state)
{
    EUSART_TypeDef * p_eusart = p_config->p_hw_config->p_eusart;

    /* Stop any ongoing LDMA transfers */
    efr32_usart_multi_ldma_stop_tx(p_config);
    efr32_usart_multi_ldma_stop_rx(p_config);

    /* Disable receiver if active */
    if (p_state->receiver_on)
    {
        EUSART_IntDisable(p_eusart, EUSART_IF_RXTO | EUSART_IF_RXOF |
                                    EUSART_IF_FERR | EUSART_IF_PERR);
        disable_rx_pin(p_config);
        p_state->receiver_on = false;
    }

    /* Disable TXC interrupt if TX was ongoing */
    EUSART_IntDisable(p_eusart, EUSART_IF_TXC);

    /* Unregister interrupt handlers */
    efr32_usart_multi_unregister_isr(p_config);

    /* Disable TX pin */
    disable_tx_pin(p_config);

    /* Reset and disable peripheral */
    reset_eusart(p_config);
    disable_clocks(p_config);

    /* Deinitialize LDMA controller (reference counted) */
    efr32_ldma_deinit();
}

/******************************************************************************
 * PRIVATE FUNCTIONS - Validation
 *****************************************************************************/

/*
 * @brief   Validate instance and get config/state pointers
 * @param   p_instance  Instance pointer
 * @param   pp_config   Output: config pointer (if valid)
 * @param   pp_state    Output: state pointer (if valid)
 * @return  Status code (USART_MULTI_SUCCESS if valid)
 */
static usart_multi_status_t validate_instance(
    const usart_multi_instance_t * p_instance,
    const eusart_config_t **       pp_config,
    eusart_state_t **              pp_state)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;

    if (p_instance == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    p_config = INSTANCE_TO_CONFIG(p_instance);
    p_state = p_config->p_state;

    if (!p_state->initialized)
    {
        return USART_MULTI_ERROR_NOT_INITIALIZED;
    }

    if (pp_config != NULL)
    {
        *pp_config = p_config;
    }
    if (pp_state != NULL)
    {
        *pp_state = p_state;
    }

    return USART_MULTI_SUCCESS;
}

/**
 * @brief Check if requested features are supported (async, 8N1, no flow control).
 *
 * @param[in] p_params Init parameters to validate.
 * @return USART_MULTI_SUCCESS or USART_MULTI_ERROR_NOT_SUPPORTED.
 */
static usart_multi_status_t validate_supported_features(
    const usart_multi_init_params_t * p_params)
{
    if (p_params->flow_control != USART_MULTI_FLOW_CONTROL_NONE ||
        p_params->mode != USART_MULTI_MODE_ASYNC ||
        p_params->power_mode != USART_MULTI_POWER_MODE_NORMAL)
    {
        return USART_MULTI_ERROR_NOT_SUPPORTED;
    }

    if (p_params->data_format.data_bits != 8 ||
        p_params->data_format.parity != USART_MULTI_PARITY_NONE ||
        p_params->data_format.stop_bits != USART_MULTI_STOP_BITS_1)
    {
        return USART_MULTI_ERROR_NOT_SUPPORTED;
    }

    return USART_MULTI_SUCCESS;
}

/**
 * @brief Validate config pointers and check if already initialized.
 *
 * @param[in] p_config Config structure to validate.
 * @return USART_MULTI_SUCCESS, USART_MULTI_ERROR_INVALID_PARAM, or USART_MULTI_ERROR_ALREADY_INITIALIZED.
 */
static usart_multi_status_t validate_config(
    const eusart_config_t * p_config)
{
    if (p_config == NULL || p_config->p_hw_config == NULL ||
        p_config->p_pin_config == NULL || p_config->p_state == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    if (p_config->p_state->initialized)
    {
        return USART_MULTI_ERROR_ALREADY_INITIALIZED;
    }

    return USART_MULTI_SUCCESS;
}

/*
 * @brief   Validate initialization parameters
 * @param   pp_instance Pointer to instance pointer
 * @param   instance_id Instance ID
 * @param   p_params    Initialization parameters
 * @param   pp_config   Output: config pointer (if valid)
 * @return  Status code (USART_MULTI_SUCCESS if valid)
 */
static usart_multi_status_t validate_init_params(
    usart_multi_instance_t **         pp_instance,
    usart_multi_instance_id_t         instance_id,
    const usart_multi_init_params_t * p_params,
    const eusart_config_t **          pp_config)
{
    const eusart_config_t * p_config;
    usart_multi_status_t status;

    /* Validate pointers */
    if (pp_instance == NULL || p_params == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Validate instance ID */
    if (instance_id >= USART_MULTI_MAX_INSTANCES)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Validate baudrate range */
    if (p_params->baudrate < EUSART_MIN_BAUDRATE ||
        p_params->baudrate > EUSART_MAX_BAUDRATE)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Check for unsupported features */
    status = validate_supported_features(p_params);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Get and validate config */
    p_config = efr32_usart_multi_get_config(instance_id);
    status = validate_config(p_config);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    *pp_config = p_config;
    return USART_MULTI_SUCCESS;
}

/******************************************************************************
 * PUBLIC API IMPLEMENTATION
 *****************************************************************************/

/**
 * @brief   Initialize UART instance from driver's static pool
 * @details On success, populates *pp_instance with pointer to instance from
 *          driver's memory pool. UART starts disabled - call
 *          usart_multi_set_enabled(true) to activate.
 * @param   pp_instance Pointer to instance pointer
 * @param   instance_id Instance ID to initialize
 * @param   p_params Initialization parameters
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Instance initialized successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer, invalid instance_id,
 *          or invalid parameters
 * @retval  USART_MULTI_ERROR_ALREADY_INITIALIZED Instance already initialized
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware initialization failed
 */
usart_multi_status_t usart_multi_init(
    usart_multi_instance_t **         pp_instance,
    usart_multi_instance_id_t         instance_id,
    const usart_multi_init_params_t * p_params)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate all parameters */
    status =
        validate_init_params(pp_instance, instance_id, p_params, &p_config);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    p_state = p_config->p_state;

    /* Clear state and error statistics */
    memset(p_state, 0, sizeof(eusart_state_t));
    memset(p_config->p_error_stats, 0, sizeof(eusart_error_stats_t));

    /* Store configuration in state */
    p_state->baudrate = p_params->baudrate;
    p_state->flow_control = p_params->flow_control;
    p_state->idle_detect_enabled = p_params->idle_detect_enabled;

    /* Initialize TX double buffer */
    DoubleBuffer_init(p_state->tx_buffers);
    p_state->tx_ongoing = false;

    /* Initialize RX double buffer */
    DoubleBuffer_init(p_state->rx_buffers);
    p_state->rx_ongoing = false;

    /* Mark as initialized but not enabled yet */
    /* NOTE: Peripheral is NOT enabled here - that happens in set_enabled(true) */
    p_state->initialized = true;
    p_state->enabled = false;
    p_state->receiver_on = false;
    p_state->enable_count = 0;

    /* Return instance pointer to caller */
    *pp_instance = CONFIG_TO_INSTANCE(p_config);

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Deinitialize UART instance
 * @details Stops all transfers, disables peripheral, and sets *pp_instance to
 * NULL
 * @param   pp_instance Pointer to instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Instance deinitialized successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or *pp_instance is NULL
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware deinitialization failed
 */
usart_multi_status_t usart_multi_deinit(usart_multi_instance_t ** pp_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;

    /* Validate parameters */
    if (pp_instance == NULL || *pp_instance == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    p_config = INSTANCE_TO_CONFIG(*pp_instance);

    if (p_config->p_state == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    p_state = p_config->p_state;

    /* Check if already uninitialized */
    if (!p_state->initialized)
    {
        return USART_MULTI_ERROR_NOT_INITIALIZED;
    }

    /* Disable hardware if it was enabled */
    if (p_state->enabled)
    {
        disable_peripheral(p_config, p_state);
    }

    /* Clear state flags */
    p_state->initialized = false;
    p_state->enabled = false;
    p_state->receiver_on = false;
    p_state->tx_ongoing = false;
    p_state->rx_ongoing = false;
    p_state->enable_count = 0;
    p_state->rx_callback = NULL;
    p_state->tx_callback = NULL;
    p_state->error_callback = NULL;
    p_state->idle_callback = NULL;

    /* Clear caller's pointer */
    *pp_instance = NULL;

    return USART_MULTI_SUCCESS;
}

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
usart_multi_status_t usart_multi_set_enabled(
    usart_multi_instance_t * p_instance, bool enabled)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate and get config/state */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Handle enable request */
    if (enabled)
    {
        p_state->enable_count++;

        /* First enable - initialize hardware */
        if (p_state->enable_count == 1)
        {
            status = enable_peripheral(p_config);
            if (status != USART_MULTI_SUCCESS)
            {
                p_state->enable_count = 0;
                return status;
            }
            p_state->enabled = true;
        }
        return USART_MULTI_SUCCESS;
    }

    /* Handle disable request - already at zero? */
    if (p_state->enable_count == 0)
    {
        return USART_MULTI_SUCCESS;
    }

    p_state->enable_count--;

    /* Last disable - tear down hardware */
    if (p_state->enable_count == 0)
    {
        disable_peripheral(p_config, p_state);
        p_state->enabled = false;
    }

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Change flow control mode
 * @param   p_instance Instance pointer
 * @param   flow_control Desired flow control mode
 * @return  Status code
 * @retval  USART_MULTI_ERROR_NOT_SUPPORTED Always returns NOT_SUPPORTED
 *
 * @note    Hardware flow control is not supported on this platform.
 */
usart_multi_status_t usart_multi_set_flow_control(
    usart_multi_instance_t *   p_instance,
    usart_multi_flow_control_e flow_control)
{
    (void) p_instance;
    (void) flow_control;

    /* Hardware flow control not supported on this platform */
    return USART_MULTI_ERROR_NOT_SUPPORTED;
}

/**
 * @brief   Queue data for transmission
 * @details Non-blocking - data is copied to internal buffer and transmitted
 *          via LDMA.
 * @param   p_instance Instance pointer
 * @param   p_buf Buffer to transmit
 * @param   len Number of bytes to transmit
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Data queued for transmission
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or len exceeds max TX
 * size
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_ENABLED UART not enabled
 * @retval  USART_MULTI_ERROR_BUFFER_FULL Inactive buffer full
 * @retval  USART_MULTI_ERROR_HARDWARE LDMA start failed
 */
usart_multi_status_t usart_multi_send_buffer(
    usart_multi_instance_t * p_instance, const void * p_buf, size_t len)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;
    EUSART_TypeDef *        p_eusart;
    uint8_t *               p_tx_buffer;

    /* Validate parameters */
    if (p_buf == NULL || len == 0)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    if (len > BUFFER_SIZE)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Check if UART is enabled */
    if (!p_state->enabled)
    {
        return USART_MULTI_ERROR_NOT_ENABLED;
    }

    p_eusart = p_config->p_hw_config->p_eusart;

    /* If TX is ongoing, queue to inactive buffer for chaining */
    if (p_state->tx_ongoing)
    {
        uint8_t * p_inactive_buffer;
        uint32_t  current_offset;

        current_offset = DoubleBuffer_getIndex(p_state->tx_buffers);

        /* Check if inactive buffer has room */
        if (current_offset + len > BUFFER_SIZE)
        {
            return USART_MULTI_ERROR_BUFFER_FULL;
        }

        /* Get inactive buffer (opposite of active) */
        if (DoubleBuffer_getActive(p_state->tx_buffers) ==
            p_state->tx_buffers.buffer_1)
        {
            p_inactive_buffer = p_state->tx_buffers.buffer_2;
        }
        else
        {
            p_inactive_buffer = p_state->tx_buffers.buffer_1;
        }

        /* Copy data to inactive buffer at current offset */
        memcpy(p_inactive_buffer + current_offset, p_buf, len);
        DoubleBuffer_incrIndex(p_state->tx_buffers, len);

        /* ISR will chain this buffer when current LDMA completes */
        return USART_MULTI_SUCCESS;
    }

    /* No TX ongoing - start fresh transfer */
    p_tx_buffer = DoubleBuffer_getActive(p_state->tx_buffers);
    memcpy(p_tx_buffer, p_buf, len);

    p_state->tx_ongoing = true;

    /* Enable TXC interrupt for completion notification */
    EUSART_IntClear(p_eusart, EUSART_IF_TXC);
    EUSART_IntEnable(p_eusart, EUSART_IF_TXC);

    /* Start LDMA transfer */
    if (!efr32_usart_multi_ldma_start_tx(p_config, p_tx_buffer, len))
    {
        EUSART_IntDisable(p_eusart, EUSART_IF_TXC);
        p_state->tx_ongoing = false;
        return USART_MULTI_ERROR_HARDWARE;
    }

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Block until all pending TX data transmitted
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS All data transmitted successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized

 */
usart_multi_status_t usart_multi_flush(usart_multi_instance_t * p_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;
    EUSART_TypeDef *        p_eusart;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    p_eusart = p_config->p_hw_config->p_eusart;

    /* Wait for LDMA transfer to complete */
    while (p_state->tx_ongoing)
    {
        /* Busy wait */
    }

    /* Wait for all data to be shifted out (TXC) */
    while (!(EUSART_StatusGet(p_eusart) & EUSART_STATUS_TXC))
    {
        /* Busy wait */
    }

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Abort ongoing TX transfer
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS TX aborted successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized

 */
usart_multi_status_t usart_multi_abort_transmit(
    usart_multi_instance_t * p_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;
    EUSART_TypeDef *        p_eusart;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    p_eusart = p_config->p_hw_config->p_eusart;

    /* Disable TXC interrupt */
    EUSART_IntDisable(p_eusart, EUSART_IF_TXC);

    /* Stop LDMA transfer */
    efr32_usart_multi_ldma_stop_tx(p_config);

    /* Swap buffer for next transmission */
    DoubleBuffer_swipe(p_state->tx_buffers);

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Start receiver
 * @details RX callback must be registered first using
 *          usart_multi_register_rx_callback(). Uses LDMA for reception
 *          with RX timeout for partial packet detection.
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Receiver started successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 * @retval  USART_MULTI_ERROR_NOT_ENABLED UART not enabled
 * @retval  USART_MULTI_ERROR_HARDWARE Hardware RX enable failed
 */
usart_multi_status_t usart_multi_enable_receiver(
    usart_multi_instance_t * p_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;
    EUSART_TypeDef *        p_eusart;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Must be enabled first */
    if (!p_state->enabled)
    {
        return USART_MULTI_ERROR_NOT_ENABLED;
    }

    /* Check if RX callback is registered */
    if (p_state->rx_callback == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Already receiving? */
    if (p_state->receiver_on)
    {
        return USART_MULTI_SUCCESS;
    }

    p_eusart = p_config->p_hw_config->p_eusart;

    /* Enable RX pin */
    enable_rx_pin(p_config);

    /* Note: RX timeout already configured in configure_eusart() during init */

    /* Enable RX timeout and error interrupts */
    EUSART_IntClear(p_eusart, EUSART_IF_RXTO | EUSART_IF_RXOF |
                              EUSART_IF_FERR | EUSART_IF_PERR);
    EUSART_IntEnable(p_eusart, EUSART_IF_RXTO | EUSART_IF_RXOF |
                               EUSART_IF_FERR | EUSART_IF_PERR);

    /* Switch to TX+RX mode */
    EUSART_Enable(p_eusart, eusartEnable);

    /* Clear pending RX interrupt */
    Sys_clearFastAppIrq(p_config->p_hw_config->rx_irqn);

    /* Mark receiver as active */
    p_state->receiver_on = true;

    /* Start LDMA RX transfer */
    if (!efr32_usart_multi_ldma_start_rx(p_config))
    {
        EUSART_IntDisable(p_eusart, EUSART_IF_RXTO);
        EUSART_Enable(p_eusart, eusartEnableTx);
        disable_rx_pin(p_config);
        p_state->receiver_on = false;
        return USART_MULTI_ERROR_HARDWARE;
    }

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Abort ongoing RX transfer and stop receiver
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Reception aborted successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_abort_receive(
    usart_multi_instance_t * p_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;
    EUSART_TypeDef *        p_eusart;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Not receiving? Nothing to do */
    if (!p_state->receiver_on)
    {
        return USART_MULTI_SUCCESS;
    }

    p_eusart = p_config->p_hw_config->p_eusart;

    /* Stop LDMA RX transfer */
    efr32_usart_multi_ldma_stop_rx(p_config);

    /* Disable RX timeout and error interrupts */
    EUSART_IntDisable(p_eusart, EUSART_IF_RXTO | EUSART_IF_RXOF |
                                EUSART_IF_FERR | EUSART_IF_PERR);

    /* Switch back to TX-only mode */
    EUSART_Enable(p_eusart, eusartEnableTx);

    /* Disable RX pin */
    disable_rx_pin(p_config);

    /* Mark receiver as inactive */
    p_state->receiver_on = false;

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Register RX data callback
 * @details Callback must be provided (cannot be NULL). Callback runs in ISR
 * context.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer or NULL callback
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_rx_callback(
    usart_multi_instance_t * p_instance, usart_multi_rx_callback_f p_callback)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* RX callback cannot be NULL (required for receiver) */
    if (p_callback == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Store callback */
    p_state->rx_callback = p_callback;

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Register TX complete callback
 * @details Pass NULL to disable. Callback runs in ISR context.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_tx_callback(
    usart_multi_instance_t * p_instance, usart_multi_tx_callback_f p_callback)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Store callback (NULL is allowed to disable) */
    p_state->tx_callback = p_callback;

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Register error callback for error notification
 * @details Pass NULL to disable. Callback runs in ISR context.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_error_callback(
    usart_multi_instance_t *     p_instance,
    usart_multi_error_callback_f p_callback)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Store callback (NULL is allowed to disable) */
    p_state->error_callback = p_callback;

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Register idle line detection callback
 * @details Pass NULL to disable. Requires idle_detect_enabled = true during
 * init. Callback runs in ISR context. Used for variable-length packet
 * detection.
 * @param   p_instance Instance pointer
 * @param   p_callback Callback function
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Callback registered successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_register_idle_callback(
    usart_multi_instance_t * p_instance, usart_multi_idle_callback_f p_callback)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Store callback (NULL is allowed to disable) */
    p_state->idle_callback = p_callback;

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Get maximum bytes per usart_multi_send_buffer() call
 * @param   p_instance Instance pointer
 * @return  Maximum TX size in bytes, or 0 if instance is NULL or not
 * initialized
 */
size_t usart_multi_get_max_tx_size(const usart_multi_instance_t * p_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return 0;
    }

    return BUFFER_SIZE;
}

/**
 * @brief   Get total UART instances available on platform
 * @return  Number of available UART instances
 */
uint8_t usart_multi_get_instance_count(void)
{
    return efr32_usart_multi_platform_get_max_instances();
}

/**
 * @brief   Retrieve accumulated error statistics
 * @param   p_instance Instance pointer
 * @param   p_stats Statistics structure to populate
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Statistics retrieved successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_get_error_stats(
    const usart_multi_instance_t * p_instance,
    usart_multi_error_stats_t *    p_stats)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate parameters */
    if (p_stats == NULL)
    {
        return USART_MULTI_ERROR_INVALID_PARAM;
    }

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Copy error stats to output structure */
    p_stats->overrun_errors = p_config->p_error_stats->overrun_errors;
    p_stats->framing_errors = p_config->p_error_stats->framing_errors;
    p_stats->parity_errors = p_config->p_error_stats->parity_errors;
    p_stats->break_errors = p_config->p_error_stats->break_errors;

    return USART_MULTI_SUCCESS;
}

/**
 * @brief   Reset all error counters to zero
 * @param   p_instance Instance pointer
 * @return  Status code
 * @retval  USART_MULTI_SUCCESS Error counters cleared successfully
 * @retval  USART_MULTI_ERROR_INVALID_PARAM NULL pointer
 * @retval  USART_MULTI_ERROR_NOT_INITIALIZED Instance not initialized
 */
usart_multi_status_t usart_multi_clear_error_stats(
    usart_multi_instance_t * p_instance)
{
    const eusart_config_t * p_config;
    eusart_state_t *        p_state;
    usart_multi_status_t    status;

    /* Validate instance */
    status = validate_instance(p_instance, &p_config, &p_state);
    if (status != USART_MULTI_SUCCESS)
    {
        return status;
    }

    /* Clear all error counters */
    memset(p_config->p_error_stats, 0, sizeof(eusart_error_stats_t));

    return USART_MULTI_SUCCESS;
}
// CPD-ON
