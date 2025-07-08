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
 *
 * @note    If provisioning configuration is found from the application
 *          persistent area, this library will take care of provisioning init.
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
 * Provisioning library callbacks are provided as custom types that fulfill
 * the signatures of provisioning callback types, without needing a
 * dependency to provisioning library, which is not needed if provisioning
 * is not used.
 */

/**
 * @brief   Provisioning end callback
 *
 * @param[in]   result  Provisioning result
 *
 * @return  True:   Apply received parameters and reboot;
 *          False:  Discard data and end provisioning process
 */
typedef bool (*setup_provisioning_end_cb_f)(uint8_t result);

/**
 * @brief   Provisioning user data callback
 *
 * @param[in]   id      Id of the received item
 * @param[in]   type    CBOR type of received item
 * @param[in]   data    Received data
 * @param[in]   len     Length of the data
 */
typedef void (*setup_provisioning_user_data_cb_f)(uint32_t id,
                                                  uint8_t type,
                                                  uint8_t * data,
                                                  uint8_t len);

/** 
 * @brief   Provisioning joining beacon RX callback
 *
 * @param[in]   beacons A buffer of joining beacons
 *
 * @return  The selected beacon
 */
typedef void * (*setup_provisioning_joining_beacon_rx_cb_f)(void * beacons);

/**
 * @brief   Application setup configuration.
 */
typedef struct app_setup_conf
{
    /**
     * @brief   Provisioning library callbacks
     */
    struct setup_provisioning_conf
    {
        /**
         * Provisioning end callback
         */
        setup_provisioning_end_cb_f end_cb;

        /**
         * Provisioning user data callback
         */
        setup_provisioning_user_data_cb_f user_data_cb;

        /**
         * Provisioning joining beacon RX callback
         */
        setup_provisioning_joining_beacon_rx_cb_f joining_beacon_rx_cb;
    } provisioning;

} app_setup_conf_t;

/**
 * @brief   Setup configuration from application persistent memory
 *
 * @note    This function will take care of initialization of Provisioning,
 *          IF provisioning config is found from persistent data.
 *
 * @return  \ref app_setup_res_e
 */
app_setup_res_e App_Setup(const app_setup_conf_t * conf);

#endif // APP_SETUP_H
