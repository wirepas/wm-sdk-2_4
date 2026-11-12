/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef APP_SETUP_H
#define APP_SETUP_H
/**
 * @brief   Application setup library
 *
 *          This library reads configuration from application persistent area,
 *          and writes it to stack using SingleMCU API.
 */

#include <stdint.h>

/**
 * @brief   Application setup return values.
 */
typedef enum
{
    /**
     * Success (provisioning initialized if config for it was given).
     */
    APP_SETUP_RES_OK,

    /**
     * No data in application persistent area.
     */
    APP_SETUP_RES_NO_DATA,

    /**
     * Error in setup, see logs for details.
     */
    APP_SETUP_RES_ERROR,

    /**
     * No application persistent area.
     */
    APP_SETUP_RES_NO_APP_PERSISTENT,

    /**
     * Data length is invalid
     */
    APP_SETUP_RES_INVALID_DATA,

    /**
     * Data version not supported.
     */
    APP_SETUP_RES_INVALID_VERSION,

    /**
     * Error in erasing application persistent area, see log for details.
     */
    APP_SETUP_RES_ERASE_ERROR,

} app_setup_res_e;

/**
 * @brief   Setup configuration from application persistent memory
 *
 * @return  \ref app_setup_res_e
 */
app_setup_res_e App_Setup(void);

#endif // APP_SETUP_H
