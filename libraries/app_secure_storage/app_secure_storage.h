/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * @file app_secure_storage.h
 *
 * The secure storage library provides the APIs for protecting the
 * content of application persistent data on internal or external flash.
 * The content of the persistent memory area is encrypted and authenticated
 * with secret device specific security keys.
 *
 * The application should use the APIs to store or retrieve the application
 * specific sensitive data. The same APIs can also be used to store and retrieve
 * the non-sensitive data, then it won't be encrypted and authenticated but
 * stored as plain text.
 *
 */

#ifndef _APP_SECURESTORAGE_H_
#define _APP_SECURESTORAGE_H_

#include <stdlib.h>
#include <stdint.h>

/**
 * \brief   List of return code
 */
typedef enum
{
    /** Operation is successful */
    APP_SECURE_STORAGE_RES_OK = 0,
    /** Error during operation */
    APP_SECURE_STORAGE_RES_ERROR = 1,
    /** Underneath storage driver is busy */
    APP_SECURE_STORAGE_RES_BUSY = 2,
    /** The storage driver is missing */
    APP_SECURE_STORAGE_RES_NODRIVER = 3,
    /** Invalid parameters */
    APP_SECURE_STORAGE_RES_PARAM = 4,
    /** Memory area doesn't exist */
    APP_SECURE_STORAGE_RES_INVALID_AREA = 5,
    /** Given data tag not found */
    APP_SECURE_STORAGE_RES_INVALID_TAG = 6,
} app_secure_storage_res_e;


/** \brief  Secure storage option flags. */
typedef enum
{
    /** Do not encrypt data */
    APP_SECURE_STORAGE_NO_ENCRYPT = (1 << 0),
    /** Do not authenticate data */
    APP_SECURE_STORAGE_NO_AUTH = (1 << 1),
} app_secure_storage_flags_e;

/**
 * \brief   Initialize secure storage area
 * \param   area_id
 *          ID of the memory area to initialize as secure storage area.
 * \return  Return code of the operation
 * \note    The secure storage area must be initialized with this function
 *          before calling any other secure storage functions.
 */
app_secure_storage_res_e App_SecureStorage_init(uint32_t area_id);

/**
 * \brief   Erase secure storage area
 * \param   area_id
 *          ID of the memory area to erase.
 * \return  Return code of the operation
 *
 */
app_secure_storage_res_e App_SecureStorage_erase(uint32_t area_id);

/**
 * \brief   Check if secure storage area exists
 * \param   area_id
 *          ID of the memory area to check.
 * \return  Return code of the operation
 *
 */
app_secure_storage_res_e App_SecureStorage_check(uint32_t area_id);

/**
 * \brief   Read data from the secure storage area
 * \param   area_id
 *          ID of the secure storage memory area to write
 * \param   tag
 *          ID of the data to read.
 * \param   data
 *          Pointer to store read data
 * \param   max_amount
 *          Number of bytes of space available for reading, actual number of
 *          bytes read returned
 * \return  Return code of the operation
 */
app_secure_storage_res_e App_SecureStorage_read(uint32_t area_id, uint32_t tag,
                                                uint8_t * data,
                                                size_t *  max_amount);

/**
 * \brief   Write data to the secure storage area
 * \param   area_id
 *          ID of the secure storage memory area to write
 * \param   tag
 *          ID of the data to write.
 * \param   data
 *          Pointer to the data to write
 * \param   len
 *          Length of data to write
 * \return  Return code of the operation
 */
app_secure_storage_res_e App_SecureStorage_write(
    uint32_t                   area_id,
    uint32_t                   tag,
    const uint8_t *            data,
    size_t                     len,
    app_secure_storage_flags_e flags);

/**
 * \brief   Delete data from the secure storage area
 * \param   area_id
 *          ID of the secure storage memory area to write
 * \param   tag
 *          ID of the data to delete.
 * \return  Return code of the operation
 */
app_secure_storage_res_e App_SecureStorage_delete(uint32_t area_id,
                                                  uint32_t tag);


#endif  //_APP_SECURESTORAGE_H_
