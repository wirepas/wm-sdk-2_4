/* Copyright 2020 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "board.h"
#include "api.h"

#include "mcu.h"
#include "i2c.h"
#include "i2c_common.h"

#include "em_i2c.h"  // Silicon Labs em_lib I2C driver

/** Declare the interrupt handler */
void I2C_IRQHandler(void);

#ifndef BOARD_I2C_SCL_GPIO_PORT
#define BOARD_I2C_SCL_GPIO_PORT BOARD_I2C_GPIO_PORT
#endif

#ifndef BOARD_I2C_SDA_GPIO_PORT
#define BOARD_I2C_SDA_GPIO_PORT BOARD_I2C_GPIO_PORT
#endif

#if defined(USE_I2C0) && defined(USE_I2C1)
#error USE_I2C0 and USE_I2C1 are mutually exclusive, both are defined
#endif

#if defined(USE_I2C1)
#define I2C_IRQn            I2C1_IRQn
#define I2C_DEV             I2C1
#define I2C_MODULE          1
#define I2C_CLKEN0_BIT      CMU_CLKEN0_I2C1

#elif defined(USE_I2C0)
#define I2C_IRQn            I2C0_IRQn
#define I2C_DEV             I2C0
#define I2C_MODULE          0
#define I2C_CLKEN0_BIT      CMU_CLKEN0_I2C0

#else
#error USE_I2C0 or USE_I2C1 must be defined
#endif

/**
 * Time taken to transfer one bit on I2C with 50% margin. This value depends
 * on the bus speed.
 * This timeout is necessary because I2C peripheral doesn't generate any errors
 * and hangs when the line is low (disconnected slave, no pull-up one the line).
 * In asynchronous mode, use I2C_status to check if transfer is finished.
 */
static uint32_t m_i2c_bit_timeout_ns;

/** Is I2C module initialized */
static volatile i2c_sm_state_e m_state = I2C_SM_ST_UNINITIALIZED;

/** Current transfer ongoing. Only one transfer supported at a time */
static volatile internal_xfer_desc m_current_xfer;

/** em_lib sequence descriptor — must persist for the entire transfer duration */
static I2C_TransferSeq_TypeDef m_seq;


/**
 * \brief   Configure I2C gpios
 * \param   pullup
 *          Activate internal EFR32 pullup on SDA and SCL
 */
static void configure_gpios(bool pullup)
{
    //Configure SCL GPIO
    hal_gpio_set(BOARD_I2C_SCL_GPIO_PORT, BOARD_I2C_SCL_PIN);
    hal_gpio_set_mode(BOARD_I2C_SCL_GPIO_PORT,
                      BOARD_I2C_SCL_PIN,
                      pullup ? GPIO_MODE_OUT_OD_PU :
                               GPIO_MODE_OUT_OD_NOPULL);

    //Configure SDA GPIO
    hal_gpio_set(BOARD_I2C_SDA_GPIO_PORT, BOARD_I2C_SDA_PIN);
    hal_gpio_set_mode(BOARD_I2C_SDA_GPIO_PORT,
                      BOARD_I2C_SDA_PIN,
                      pullup ? GPIO_MODE_OUT_OD_PU :
                               GPIO_MODE_OUT_OD_NOPULL);

    // Route GPIO pins to I2C module
    GPIO->I2CROUTE[I2C_MODULE].SDAROUTE = (GPIO->I2CROUTE[I2C_MODULE].SDAROUTE & ~_GPIO_I2C_SDAROUTE_MASK)
                                        | (BOARD_I2C_SDA_GPIO_PORT << _GPIO_I2C_SDAROUTE_PORT_SHIFT)
                                        | (BOARD_I2C_SDA_PIN << _GPIO_I2C_SDAROUTE_PIN_SHIFT);
    GPIO->I2CROUTE[I2C_MODULE].SCLROUTE = (GPIO->I2CROUTE[I2C_MODULE].SCLROUTE & ~_GPIO_I2C_SCLROUTE_MASK)
                                        | (BOARD_I2C_SCL_GPIO_PORT << _GPIO_I2C_SCLROUTE_PORT_SHIFT)
                                        | (BOARD_I2C_SCL_PIN << _GPIO_I2C_SCLROUTE_PIN_SHIFT);
    GPIO->I2CROUTE[I2C_MODULE].ROUTEEN = GPIO_I2C_ROUTEEN_SDAPEN | GPIO_I2C_ROUTEEN_SCLPEN;
}

/**
 * \brief   Release I2C gpios (SDA and SCL)
 */
static void release_gpios(void)
{
    // Disable routing of SDA and SCL to the I2C peripheral
    GPIO->I2CROUTE[I2C_MODULE].SDAROUTE = _GPIO_I2C_SDAROUTE_RESETVALUE;
    GPIO->I2CROUTE[I2C_MODULE].SCLROUTE = _GPIO_I2C_SCLROUTE_RESETVALUE;
    GPIO->I2CROUTE[I2C_MODULE].ROUTEEN = _GPIO_LETIMER_ROUTEEN_RESETVALUE;

    // Restore SDA GPIO
    hal_gpio_set_mode(BOARD_I2C_SDA_GPIO_PORT,
                      BOARD_I2C_SDA_PIN,
                      GPIO_MODE_DISABLED);
    hal_gpio_clear(BOARD_I2C_SDA_GPIO_PORT, BOARD_I2C_SDA_PIN);

    // Restore SCL GPIO
    hal_gpio_set_mode(BOARD_I2C_SCL_GPIO_PORT,
                      BOARD_I2C_SCL_PIN,
                      GPIO_MODE_DISABLED);
    hal_gpio_clear(BOARD_I2C_SCL_GPIO_PORT, BOARD_I2C_SCL_PIN);
}

/**
 * \brief   Enable or disable I2C peripheral register access clock gate
 * \param   enable
 *          True to enable clock, false to disable
 */
static void enable_clock(bool enable)
{
#if (_SILICON_LABS_32B_SERIES_2_CONFIG >= 2)
    if (enable)
    {
        CMU->CLKEN0_SET = I2C_CLKEN0_BIT;
    }
    else
    {
        CMU->CLKEN0_CLR = I2C_CLKEN0_BIT;
    }
#else
    // EFR32xG21 does not have clock gates for register access
    (void)enable;
#endif
}

#pragma GCC push_options
#pragma GCC optimize("O0")
/**
 * \brief   I2C bus recovery: send up to 9 SCL clock pulses to release a slave
 *          holding SDA low, then generate a STOP condition.
 */
static void i2c_bus_recovery(void)
{
    /* Half SCL period for bit-bang timing. */
    uint32_t half_period_us = m_i2c_bit_timeout_ns / 2000;
    if (half_period_us == 0)
    {
        half_period_us = 1;
    }

    /* Ensure GPIO DOUT is high for both pins before taking manual control */
    hal_gpio_set(BOARD_I2C_SCL_GPIO_PORT, BOARD_I2C_SCL_PIN);
    hal_gpio_set(BOARD_I2C_SDA_GPIO_PORT, BOARD_I2C_SDA_PIN);

    /* Disable I2C routing: GPIO output register now drives SCL and SDA.
     * Pins are already open-drain from configure_gpios(). */
    GPIO->I2CROUTE[I2C_MODULE].ROUTEEN &= ~(GPIO_I2C_ROUTEEN_SCLPEN |
                                            GPIO_I2C_ROUTEEN_SDAPEN);

    /* Send up to 9 SCL clock pulses */
    for (uint8_t i = 0; i < 9; i++)
    {
        app_lib_time_timestamp_hp_t end;

        hal_gpio_clear(BOARD_I2C_SCL_GPIO_PORT, BOARD_I2C_SCL_PIN);
        end = lib_time->addUsToHpTimestamp(lib_time->getTimestampHp(),
                                           half_period_us);
        while (lib_time->isHpTimestampBefore(lib_time->getTimestampHp(), end));

        hal_gpio_set(BOARD_I2C_SCL_GPIO_PORT, BOARD_I2C_SCL_PIN);
        end = lib_time->addUsToHpTimestamp(lib_time->getTimestampHp(),
                                           half_period_us);
        while (lib_time->isHpTimestampBefore(lib_time->getTimestampHp(), end));

        /* Stop early if slave released SDA */
        if (hal_gpio_get(BOARD_I2C_SDA_GPIO_PORT, BOARD_I2C_SDA_PIN))
        {
            break;
        }
    }

    /* Re-enable GPIO routing to I2C peripheral */
    GPIO->I2CROUTE[I2C_MODULE].ROUTEEN |= GPIO_I2C_ROUTEEN_SCLPEN |
                                          GPIO_I2C_ROUTEEN_SDAPEN;
    /* I2C Abort */
    I2C_DEV->CMD = I2C_CMD_CLEARPC | I2C_CMD_CLEARTX | I2C_CMD_ABORT;

    /* I2C Bus Reset */
    I2C_DEV->CMD = I2C_CMD_START | I2C_CMD_STOP;
}

/**
 * \brief   I2C if device is busy, issue abort command.
 *          If SDA is held low by a slave perform 9-clock
 *          bus recovery sequence to release the stuck slave.
 */
static void i2c_perform_recovery_if_needed(void)
{
    enable_clock(true);
    /* if I2C device is busy, issue abort command. */
    if (I2C_DEV->STATE & I2C_STATE_BUSY)
    {
        I2C_DEV->CMD = I2C_CMD_CLEARPC | I2C_CMD_CLEARTX | I2C_CMD_ABORT;
    }

    /* If SDA is held low by a slave, request 9-clock recovery */
    if (!hal_gpio_get(BOARD_I2C_SDA_GPIO_PORT, BOARD_I2C_SDA_PIN))
    {
        i2c_bus_recovery();
    }
    enable_clock(false);
}
#pragma GCC pop_options

/**
 * \brief   Map em_lib transfer result to Wirepas i2c_res_e
 */
static i2c_res_e map_emlib_result(I2C_TransferReturn_TypeDef ret)
{
    switch (ret)
    {
        case i2cTransferDone:
            return I2C_RES_OK;
        case i2cTransferNack:
            /* em_lib does not distinguish ANACK vs DNACK.
             * Read STATE here: transfer is complete so the register has settled.
             * ADDRACK phase means address was NACKed; otherwise data was NACKed. */
            if ((I2C_DEV->STATE & _I2C_STATE_STATE_MASK) == I2C_STATE_STATE_ADDRACK)
            {
                return I2C_RES_ANACK;
            }
            return I2C_RES_DNACK;
        default:
            return I2C_RES_BUS_HANG;
    }
}

i2c_res_e I2C_init(i2c_conf_t * conf_p)
{
    if (m_state != I2C_SM_ST_UNINITIALIZED)
    {
        return I2C_RES_ALREADY_INITIALIZED;
    }

    if (conf_p->clock == 0)
    {
        return I2C_RES_INVALID_CONFIG;
    }

    // Turn on clock to the I2C peripheral during configuration
    enable_clock(true);

    // Disable interrupts and clear pending flags before touching peripheral
    I2C_IntDisable(I2C_DEV, _I2C_IEN_MASK);
    I2C_IntClear(I2C_DEV, _I2C_IF_MASK);

    // Enable I2C IRQ
    Sys_clearFastAppIrq(I2C_IRQn);
    Sys_enableFastAppIrq(I2C_IRQn,
                         APP_LIB_SYSTEM_IRQ_PRIO_HI,
                         I2C_IRQHandler);

    // Configure the GPIOs before enabling I2C peripheral so that SDA/SCL
    // inputs are routed (and pulled HIGH) when the peripheral is enabled.
    // This prevents the I2C from detecting a spurious "bus busy" condition.
    configure_gpios(conf_p->pullup);

    // Initialize I2C peripheral via em_lib (handles CTRL, CLKDIV, enable)
    I2C_Init_TypeDef init = I2C_INIT_DEFAULT;
    init.enable  = true;
    init.master  = true;
    init.refFreq = 0;       // use currently configured reference clock
    init.freq    = conf_p->clock;
    init.clhr    = i2cClockHLRStandard;

    I2C_Init(I2C_DEV, &init);

    // Issue an ABORT command to make sure the peripheral is in a known state
    I2C_DEV->CMD = I2C_CMD_CLEARPC | I2C_CMD_CLEARTX | I2C_CMD_ABORT;

    // Configuration done, turn off clock to the I2C peripheral
    enable_clock(false);

    /* Add 50% margin to timeout calculation */
    m_i2c_bit_timeout_ns = 1500000000 / conf_p->clock;

    m_current_xfer.free = true;

    // Mark I2C driver as initialized
    m_state = I2C_SM_ST_IDLE;

    return I2C_RES_OK;
}

i2c_res_e I2C_close(void)
{
    if (m_state == I2C_SM_ST_UNINITIALIZED)
    {
        return I2C_RES_NOT_INITIALIZED;
    }

    m_state = I2C_SM_ST_UNINITIALIZED;

    // Turn on clock to the I2C peripheral during configuration
    enable_clock(true);

    // Disable all I2C interrupts and clear pending flags
    I2C_IntDisable(I2C_DEV, _I2C_IEN_MASK);
    I2C_IntClear(I2C_DEV, _I2C_IF_MASK);

    // Disable I2C IRQ
    Sys_disableAppIrq(I2C_IRQn);

    // Issue an ABORT command to make sure the peripheral is in a known state
    I2C_DEV->CMD = I2C_CMD_CLEARPC | I2C_CMD_CLEARTX | I2C_CMD_ABORT;

    // Set all gpios as default configuration
    release_gpios();

    // Disable I2C peripheral
    I2C_DEV->EN_CLR = I2C_EN_EN;
    I2C_DEV->CTRL = _I2C_CTRL_RESETVALUE;

    // Configuration done, turn off clock to the I2C peripheral
    enable_clock(false);

    return I2C_RES_OK;
}

#pragma GCC push_options
#pragma GCC optimize("O0")
i2c_res_e I2C_transfer(i2c_xfer_t * xfer_p, i2c_on_transfer_done_cb_f cb)
{
    if (m_state == I2C_SM_ST_UNINITIALIZED)
    {
        return I2C_RES_NOT_INITIALIZED;
    }

    // Check if a transfer is already ongoing
    if (!m_current_xfer.free)
    {
        return I2C_RES_BUSY;
    }

    // Check transfer
    if (((xfer_p->read_ptr != NULL) && (xfer_p->read_size == 0)) ||
        ((xfer_p->read_ptr == NULL) && (xfer_p->read_size != 0)) ||
        ((xfer_p->write_ptr != NULL) && (xfer_p->write_size == 0)) ||
        ((xfer_p->write_ptr == NULL) && (xfer_p->write_size != 0)))
    {
        return I2C_RES_INVALID_XFER;
    }

    // Setup the transfer descriptor
    m_current_xfer.client_xfer = xfer_p;
    m_current_xfer.cb          = cb;
    m_current_xfer.res         = I2C_RES_OK;
    m_current_xfer.free        = false;
    m_current_xfer.done        = false;

    i2c_perform_recovery_if_needed();

    // Build em_lib transfer sequence from Wirepas transfer descriptor.
    // Use module-level m_seq (not a local): em_lib stores a pointer to this
    // struct and dereferences it from the IRQ handler — a stack variable would
    // become a dangling pointer after I2C_transfer() returns.
    m_seq.addr = (uint16_t)(xfer_p->address << 1);

    if ((xfer_p->write_ptr != NULL) && (xfer_p->read_ptr != NULL))
    {
        m_seq.flags       = I2C_FLAG_WRITE_READ;
        m_seq.buf[0].data = xfer_p->write_ptr;
        m_seq.buf[0].len  = xfer_p->write_size;
        m_seq.buf[1].data = xfer_p->read_ptr;
        m_seq.buf[1].len  = xfer_p->read_size;
    }
    else if (xfer_p->write_ptr != NULL)
    {
        m_seq.flags       = I2C_FLAG_WRITE;
        m_seq.buf[0].data = xfer_p->write_ptr;
        m_seq.buf[0].len  = xfer_p->write_size;
        m_seq.buf[1].data = NULL;
        m_seq.buf[1].len  = 0;
    }
    else
    {
        m_seq.flags       = I2C_FLAG_READ;
        m_seq.buf[0].data = xfer_p->read_ptr;
        m_seq.buf[0].len  = xfer_p->read_size;
        m_seq.buf[1].data = NULL;
        m_seq.buf[1].len  = 0;
    }

    // Turn on clock and kick off the transfer
    enable_clock(true);
    m_state = I2C_SM_ST_BUSY;

    I2C_TransferReturn_TypeDef ret = I2C_TransferInit(I2C_DEV, &m_seq);

    if (ret < 0)
    {
        // Immediate error (usage fault or similar)
        m_state = I2C_SM_ST_IDLE;
        m_current_xfer.free = true;
        enable_clock(false);
        return I2C_RES_BUS_HANG;
    }
    // ret == i2cTransferInProgress: IRQ will drive the rest

    // Is it a blocking call
    if (m_current_xfer.cb == NULL)
    {
        app_lib_time_timestamp_hp_t end;

        /* Timeout calculation (timeout is in us):
         * - Start + Address + R/W bit is 10 bits
         * - For each byte read or written add 1 ack bit so 9 bits
         */
        uint32_t timeout = ((10 + xfer_p->read_size * 9 + xfer_p->write_size * 9) *
                            m_i2c_bit_timeout_ns) / 1000;

        end = lib_time->addUsToHpTimestamp(lib_time->getTimestampHp(),
                                           timeout);

        // Active wait until end of transfer or timeout
        while (!m_current_xfer.done &&
               lib_time->isHpTimestampBefore(lib_time->getTimestampHp(), end));

        m_current_xfer.free = true;

        if (!m_current_xfer.done)
        {
            return I2C_RES_BUS_HANG;
        }
        else
        {
            return m_current_xfer.res;
        }
    }

    return I2C_RES_OK;
}
#pragma GCC pop_options

i2c_res_e I2C_status(void)
{
    if (m_state == I2C_SM_ST_UNINITIALIZED)
    {
        return I2C_RES_NOT_INITIALIZED;
    }
    else if (!m_current_xfer.free)
    {
        return I2C_RES_BUSY;
    }
    else
    {
        return I2C_RES_OK;
    }
}

#pragma GCC push_options
#pragma GCC target("general-regs-only")
#pragma GCC optimize("O0")
/**
 * \brief   Function to handle the I2C Interrupt.
 *          Delegates to em_lib I2C_Transfer() which uses IF flags (not STATE
 *          register) to advance the protocol state machine, avoiding the
 *          hardware race where STATE is updated after the interrupt fires.
 */
void __attribute__((__interrupt__)) I2C_IRQHandler(void)
{
    /* Re-enable the clock unconditionally before any peripheral register access.
     * A tail-chained/stale NVIC pending bit can fire this handler after
     * enable_clock(false) was called at the end of the previous invocation.
     * enable_clock(true) is idempotent when the clock is already on. */
    enable_clock(true);

    /* If no transfer is in progress this is a spurious interrupt.
     * Clear all pending IF flags so we don't re-enter immediately, then
     * gate the clock and return without touching em_lib state. */
    if (m_state != I2C_SM_ST_BUSY)
    {
        I2C_IntClear(I2C_DEV, _I2C_IF_MASK);
        enable_clock(false);
        return;
    }

    I2C_TransferReturn_TypeDef ret = I2C_Transfer(I2C_DEV);

    if (ret == i2cTransferInProgress)
    {
        // em_lib has re-armed the appropriate interrupt; nothing to do
        return;
    }

    // Transfer complete or error
    m_state = I2C_SM_ST_IDLE;

    if (!m_current_xfer.free)
    {
        m_current_xfer.res  = map_emlib_result(ret);
        m_current_xfer.done = true;
        m_current_xfer.free = true;

        if (m_current_xfer.cb != NULL)
        {
            // Call client callback; this may start a new transfer
            m_current_xfer.cb(m_current_xfer.res, m_current_xfer.client_xfer);
        }
    }

    if (m_state == I2C_SM_ST_IDLE)
    {
        // No new transfer was started in the callback, gate the clock off
        enable_clock(false);
    }
}
#pragma GCC pop_options
