/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * @file    events.c
 * @brief   Main code to process the application callbacks.
 */

#include <stdlib.h>
#include "api.h"
#include "measurements.h"
#include "shared_data.h"
#include "event.h"
#include "app_scheduler.h"
#include "uart_print.h"

#define MEASUREMENTS_ENDPOINTS 17

#define DEBUG_LOG_MODULE_NAME "EVENT"
#define DEBUG_LOG_MAX_LEVEL   LVL_INFO
#include "debug_log.h"
typedef enum
{
    MEASUREMENTS_INDEX_NODE     = 0,  // First element in the meas table
    MEASUREMENTS_INDEX_NEXT_HOP = 1,  // Second element in the meas table
    MEASUREMENTS_INDEX_MAX      = 2   // Number of fields in the enum
} measurement_index_t;

/**
    @brief      Processing of a network/cluster beacon.

            The observed beacon is matched against the beacon table. If it is
            found, the rss value is updated, otherwise it is append (if there
            is enough space).

    @param[in]  beacon  The received network/cluster beacon

*/
void Event_beacon_reception(const app_lib_state_beacon_rx_t * beacon)
{
    lib_system->enterCriticalSection();
    Measurements_insert_beacon(beacon);  // defines lookup size
    lib_system->exitCriticalSection();
}


/**
    @brief      Processing of a network scan edge.

            When a network scan is performed, the application bundles all the
            available beacons in positioning measurement payloads and
            dispatches them towards a sink.

*/
void Event_network_scan_end()
{
    uint8_t                len_payload = 0;
    const uint8_t *        ptr_payload = 0;
    app_lib_data_to_send_t payload;

    lib_system->enterCriticalSection();
    ptr_payload = Measurements_payload_init();
    Measurements_payload_add_beacon_data();
    len_payload = Measurements_payload_length();

    payload.bytes         = ptr_payload;
    payload.num_bytes     = len_payload;
    payload.dest_address  = APP_ADDR_ANYSINK;
    payload.src_endpoint  = MEASUREMENTS_ENDPOINTS;
    payload.dest_endpoint = MEASUREMENTS_ENDPOINTS;
    payload.qos           = APP_LIB_DATA_QOS_HIGH;
    payload.flags         = APP_LIB_DATA_SEND_FLAG_NONE;
    payload.tracking_id   = APP_LIB_DATA_NO_TRACKING_ID;

    Shared_Data_sendData(&payload, NULL);

    lib_system->exitCriticalSection();

    App_Scheduler_addTask_execTime(print_network_beacons_list, APP_SCHEDULER_SCHEDULE_ASAP, 100);
}
