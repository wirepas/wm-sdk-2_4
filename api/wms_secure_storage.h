/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * \file wms_secure_storage.h
 *
 * This library implements encrypted and authenticated record storage on top of
 * the memory area library. Records can vary in length and are identified by
 * 32-bit tags. Records can be read, written, or deleted.
 *
 * Tags are similar to filenames. A tool called @ref name_to_uint32_py is
 * provided to convert up to six-character names to uint32 values and back. It
 * is recommended to use this tool to generate tag values instead of choosing
 * arbitrary numbers.
 *
 * Applications have a predefined secure storage area for storing parameters.
 * Additional areas may be created for special purposes.
 *
 * Library services are accessed through the
 * \ref app_lib_secure_storage_t "lib_secure_storage" handle.
 */
#ifndef APP_LIB_SECURE_STORAGE_H_
#define APP_LIB_SECURE_STORAGE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "wms_app.h"
#include "wms_memory_area.h"

/** \brief Library symbolic name */
#define APP_LIB_SECURE_STORAGE_NAME 0x74be4415  //!< "SECURE"

/** \brief Maximum supported library version */
#define APP_LIB_SECURE_STORAGE_VERSION 0x200

/** \brief  Secure storage initialization command */
typedef enum
{
    /** Initialize if not already initialized */
    APP_LIB_SECURE_STORAGE_CMD_INIT = 0,
    /** Use as-is, return error if not initialized */
    APP_LIB_SECURE_STORAGE_CMD_NO_INIT = 1,
    /** Unconditional initialization (erase) */
    APP_LIB_SECURE_STORAGE_CMD_ERASE = 2,
} app_lib_secure_storage_command_e;

/** \brief  Secure storage record tag definition
 *  \note   Use tools/genlibname.py to generate meaningful tag values
 */
typedef uint32_t app_lib_secure_storage_record_tag_t;

/** \brief  Secure storage record option flags */
typedef enum
{
    /** Do not encrypt record contents */
    APP_LIB_SECURE_STORAGE_RECORD_FLAG_NO_ENCRYPT = (1 << 0),
    /** Do not authenticate record contents */
    APP_LIB_SECURE_STORAGE_RECORD_FLAG_NO_AUTH = (1 << 1),
} app_lib_secure_storage_record_flags_e;

/**
 * \brief  Initialize secure storage area or check if already initialized
 * \param  id
 *         ID of the memory area to initialize
 * \param  cmd
 *         Command to initialize or check secure storage area
 * \param  flags
 *         For future use, must be 0
 * \return Result code, \ref APP_LIB_MEM_AREA_RES_OK if successful,
 *         \ref APP_LIB_MEM_AREA_RES_INVALID_TAG if area is not initialized when
 *         command is \ref APP_LIB_SECURE_STORAGE_CMD_NO_INIT,
 *         \ref APP_LIB_MEM_AREA_RES_NODRIVER if encryption or key management
 *         could not be initialized, \ref APP_LIB_MEM_AREA_RES_PARAM for
 *         an error in parameters. See \ref app_lib_mem_area_res_e for memory
 *         area related result codes.
 * \note   This function must be called before calling any other secure storage
 *         functions. It is safe to call this function multiple times, e.g., to
 *         erase an already initialized secure storage memory area.
 * \note   Only areas of type \ref APP_LIB_MEM_AREA_TYPE_USER are valid.
 * \note   Accessing the memory area directly with memory area functions
 *         after it has been initialized as secure storage is undefined.
 */
typedef app_lib_mem_area_res_e (*app_lib_secure_storage_init_area_f)(
    app_lib_mem_area_id_t id, app_lib_secure_storage_command_e cmd,
    uint32_t flags);

/**
 * \brief  Read a record from the secure storage area
 * \param  id
 *         ID of the memory area to read
 * \param  tag
 *         Tag of record to read
 * \param  to
 *         Pointer in RAM to read the record contents
 * \param  max_amount
 *         Number of bytes of space available for reading, actual number of
 *         bytes read returned
 * \return Result code, \ref APP_LIB_MEM_AREA_RES_OK if successful,
 *         \ref APP_LIB_MEM_AREA_RES_INVALID_TAG if record doesn't exist,
 *         \ref APP_LIB_MEM_AREA_RES_PARAM if max_amount is too small for the
 *         record contents, or other parameter error.
 *         See \ref app_lib_mem_area_res_e for memory area related result codes.
 */
typedef app_lib_mem_area_res_e (*app_lib_secure_storage_read_record_f)(
    app_lib_mem_area_id_t id, app_lib_secure_storage_record_tag_t tag,
    void * to, size_t * max_amount);

/**
 * \brief  Write a record to the secure storage area, overwriting any previous
 *         version with the same record tag
 * \param  id
 *         ID of the memory area to write
 * \param  tag
 *         Tag of record to write. Previous record of the same tag
 *         will be overwritten
 * \param  from
 *         Pointer in RAM to the data to be written
 * \param  amount
 *         Number of bytes to write
 * \param  flags
 *         Record option flags, see \ref app_lib_secure_storage_record_flags_e
 * \return Result code, \ref APP_LIB_MEM_AREA_RES_OK if successful,
 *         \ref APP_LIB_MEM_AREA_RES_INVALID_TAG if
 *         not enough space available for the new record,
 *         \ref APP_LIB_MEM_AREA_RES_PARAM for an error in parameters,
 *         \ref APP_LIB_MEM_AREA_RES_NODRIVER for a write verify error.
 *         See \ref app_lib_mem_area_res_e for memory area related result codes.
 */
typedef app_lib_mem_area_res_e (*app_lib_secure_storage_write_record_f)(
    app_lib_mem_area_id_t               id,
    app_lib_secure_storage_record_tag_t tag,
    const void *                        from,
    size_t                              amount,
    uint32_t                            flags);

/**
 * \brief  Delete a record in the secure storage area
 * \param  id
 *         ID of the memory area to write
 * \param  tag
 *         Tag of record to delete
 * \return Result code, \ref APP_LIB_MEM_AREA_RES_OK if successful,
 *         \ref APP_LIB_MEM_AREA_RES_INVALID_TAG if record doesn't exist.
 *         See \ref app_lib_mem_area_res_e for memory area related result codes.
 */
typedef app_lib_mem_area_res_e (*app_lib_secure_storage_delete_record_f)(
    app_lib_mem_area_id_t id, app_lib_secure_storage_record_tag_t tag);

/**
 * \brief       List of library functions
 */
typedef struct
{
    app_lib_secure_storage_init_area_f     initArea;
    app_lib_secure_storage_read_record_f   readRecord;
    app_lib_secure_storage_write_record_f  writeRecord;
    app_lib_secure_storage_delete_record_f deleteRecord;
} app_lib_secure_storage_t;

#endif /* APP_LIB_SECURE_STORAGE_H_ */
