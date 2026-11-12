/* Copyright 2023 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef I2C_COMMON_H_
#define I2C_COMMON_H_

#if !defined(USE_I2C0) && !defined(USE_I2C1) && !defined(USE_I2C2) && !defined(USE_I2C3)
#define USE_I2C1
#endif

/** Internal transfer description */
typedef struct
{
    i2c_xfer_t *                client_xfer;    //< Transfer asked by client
    i2c_on_transfer_done_cb_f   cb;             //< Callback to call at end of transfer
    i2c_res_e                   res;            //< Result of I2C transfer
    bool                        free;           //< False if transfer ongoing
    bool                        done;           //< Is transfer done (blocking mode)
} internal_xfer_desc;

/** I2C driver state */
typedef enum
{
    I2C_SM_ST_UNINITIALIZED = 0,
    I2C_SM_ST_IDLE,
    I2C_SM_ST_BUSY
} i2c_sm_state_e;


#endif //I2C_COMMON_H_
