/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 */

/*
 * \file    app.c
 * \brief   This file provides an example of persistent data migration to a
 * secure storage format. Application persistent memory area is converted to a
 * secure storage area and content writtent to a secure storage record(s).
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "api.h"
#include "node_configuration.h"
#include "app_secure_storage.h"
#include "app_persistent.h"

#define DEBUG_LOG_MODULE_NAME "SECURE_STORAGE_APP"
/** To activate logs, configure the following line with "LVL_INFO". */
#define DEBUG_LOG_MAX_LEVEL LVL_INFO

#include "debug_log.h"

/**  ID of memory area which is converted to secure storage area. */
#define APP_PERSISTENT_MEMORY_AREA_ID 0x8AE573BA

/** Test data to be stored in persistent memory area */
#define PERSISTENT_TEST_DATA     "PERMANENT DATA STORAGE"
#define PERSISTENT_TEST_DATA_LEN (sizeof(PERSISTENT_TEST_DATA) - 1)

/** Persistent data structure in this example */
typedef struct persistent_data
{
    uint8_t write[PERSISTENT_TEST_DATA_LEN];
    uint8_t read[PERSISTENT_TEST_DATA_LEN];
    size_t  len;
} persistent_data_t;

/** Populate pesistent test data */
persistent_data_t persistent_data = { .write = { PERSISTENT_TEST_DATA },
                                      .read  = { 0 },
                                      .len   = PERSISTENT_TEST_DATA_LEN };

/** Test record length needs to be dividible by 4 */
#define RECORD_TEST_DATA_LEN ((PERSISTENT_TEST_DATA_LEN + 3) & ~3)

/** Test record structure */
typedef struct test_record
{
    const uint32_t area_id;
    const uint32_t tag;
    uint8_t        data[RECORD_TEST_DATA_LEN];  // Needs to be dividible by 4
    size_t         len;                         // Needs to be dividible by 4
    uint32_t       flags;
} test_record_t;

/** Populate secure storage record */
test_record_t test_record = { .area_id = APP_PERSISTENT_MEMORY_AREA_ID,
                              .tag     = 0x12347856,
                              .data    = { 0 },
                              .len     = RECORD_TEST_DATA_LEN,
                              .flags   = 0 };

/**
 * \brief   Initialization callback for application
 *
 * This function is called after hardware has been initialized but the
 * stack is not yet running.
 *
 */
void App_init(const app_global_functions_t * functions)
{
    (void) functions;
    LOG_INIT();

    LOG(LVL_INFO, "Data migration started\n");

    /** Read data from the peristent data area, if there is no data write test
        data to it */
    if (App_Persistent_read(&persistent_data.read[0], persistent_data.len)
        != APP_PERSISTENT_RES_OK)
    {
        LOG(LVL_ERROR, "Write test data to persistent area\n");

        /** Initialize persistent data area */
        if (App_Persistent_init() != APP_PERSISTENT_RES_OK)
        {
            LOG(LVL_ERROR, "Persistent init failed\n");
            return;
        }

        /** Write raw data to the peristent data area */
        if (App_Persistent_write(&persistent_data.write[0], persistent_data.len)
            != APP_PERSISTENT_RES_OK)
        {
            LOG(LVL_ERROR, "Persistent write failed\n");
            return;
        }

        /** Not really needed but for demonstration purposeread raw data back
         * from the peristent data area */
        if (App_Persistent_read(&persistent_data.read[0], persistent_data.len)
            != APP_PERSISTENT_RES_OK)
        {
            LOG(LVL_ERROR, "Persistent read failed\n");
            return;
        }

        /** Make sure we read the same data we wrote */
        if (memcmp(&persistent_data.write,
                   &persistent_data.read,
                   persistent_data.len)
            != 0)
        {
            LOG(LVL_ERROR, "Pesistent content not correct\n");
            return;
        }
    }

    /** Now we create the secure storage area */
    if (App_SecureStorage_init(APP_PERSISTENT_MEMORY_AREA_ID)
        != APP_SECURE_STORAGE_RES_OK)
    {
        LOG(LVL_ERROR, "SecureStorage init failed\n");
        return;
    }

    /** Write the data read from the peristet to the secure storage area
     * with record tag 0x12347856 */
    if (App_SecureStorage_write(APP_PERSISTENT_MEMORY_AREA_ID,
                                test_record.tag,
                                &persistent_data.read[0],
                                test_record.len,
                                test_record.flags)
        != APP_SECURE_STORAGE_RES_OK)
    {
        LOG(LVL_ERROR, "SecureStorage write failed\n");
        return;
    }

    /** Read the data back from secures storage and verify content is correct */
    if ((App_SecureStorage_read(test_record.area_id,
                                test_record.tag,
                                &test_record.data[0],
                                &test_record.len)
         != APP_SECURE_STORAGE_RES_OK)
        && (test_record.len != sizeof(persistent_data.write))
        && (memcmp(&test_record.data,
                   &persistent_data.write,
                   sizeof(persistent_data.write))
            != 0))
    {
        LOG(LVL_ERROR, "SecureStorage read failed\n");
        return;
    }

    /** Not really needed but for demonstration purpose try to read raw data
     * read from the peristet area which should fail now since it is replaced
     * with the secure storage area */
    if (App_Persistent_read(&persistent_data.read[0], persistent_data.len)
        == APP_PERSISTENT_RES_OK)
    {
        LOG(LVL_ERROR, "Persistent area still exist\n");
        return;
    }
    LOG(LVL_INFO, "SecureStorage test successful\n");
}
