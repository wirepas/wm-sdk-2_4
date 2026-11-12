/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * @file    measurements.c
 * @brief   Handles the processing and dispatch of measurements.
 */


#include <stdlib.h>

#include "api.h"
#include "measurements.h"
#include "app_scheduler.h"

#define DEBUG_LOG_MODULE_NAME "MEASURE"
#define DEBUG_LOG_MAX_LEVEL   LVL_DEBUG
#include "debug_log.h"

typedef enum
{
    MEASUREMENTS_INDEX_NODE     = 0,  // First element in the meas table
    MEASUREMENTS_INDEX_NEXT_HOP = 1,  // Second element in the meas table
    MEASUREMENTS_INDEX_MAX      = 2   // Number of fields in the enum
} measurement_index_t;

static measurement_payload_t m_payload;     // buffer to help build the payload
measurement_table_t   m_meas_table;  // stores nw/cb data
static app_addr_t            m_node_addr;
static app_addr_t            m_next_hop;
static measurement_header_sequence_t seq_id;

/**
    @brief      Clears the measurement table.
*/
void Measurements_table_reset(void)
{
    memset(&m_meas_table, '\0', sizeof(m_meas_table));
    /** We include node & next hop measurements */
    m_meas_table.num_beacons = MEASUREMENTS_INDEX_MAX;

    lib_settings->getNodeAddress(&m_node_addr);

    app_lib_state_route_info_t info;
    lib_state->getRouteInfo(&info);
    m_next_hop = info.next_hop;
}

/**
    @brief      Clears the payload buffer.
*/
static void Measurements_payload_reset(void)
{
    memset(&m_payload, '\0', sizeof(m_payload));
    m_payload.ptr = (uint8_t *) &m_payload.bytes;
}

/**
    @brief      Copies a set of bytes to the payload buffer.

                This function keeps track of the payload length by moving
                the internal pointer further down the array.

    @param      from  The memory address where to copy bytes from
    @param[in]  len   The amount of bytes to copy
*/
static void Measurements_payload_copy(void * from, uint8_t len)
{
    memcpy(m_payload.ptr, from, len);
    m_payload.ptr += len;
    Measurements_payload_length();
}


/**
    @brief      Cleans up the beacon measurement table
*/
void Measurements_init(void)
{
    Measurements_table_reset();
    Measurements_payload_reset();
}


/**
    @brief      Initialises the payload buffer for the next message.

    @return     Pointer to the current edge of the payload
*/
const uint8_t * Measurements_payload_init(void)
{
    seq_id++;

    Measurements_payload_reset();
    Measurements_payload_copy(&(seq_id), sizeof(measurement_header_sequence_t));

    return (const uint8_t *) &m_payload.bytes;
}


/**
    @brief      Search through the measurement table for the entry with
                the smallest path loss
*/
static void update_min(void)
{
    uint8_t i                  = 0;
    m_meas_table.min_path_loss = 0;  // forces a minimum refresh

    /** update minimum, ignore node and next_hop fields */
    for (i = MEASUREMENTS_INDEX_MAX; i < m_meas_table.num_beacons; i++)
    {
        /** updates minimum location */
        if (m_meas_table.beacons[i].path_loss < m_meas_table.min_path_loss)
        {
            m_meas_table.min_index = i;
            m_meas_table.min_path_loss   = m_meas_table.beacons[i].path_loss;
        }
    }
}


/**
    @brief      Inserts a beacon measurement to the table.

                This method guarantees an unique insertion of a new beacon.
                If the beacon was observed previously, its value will be
                overwritten.
                When there is not enough space for a new beacon, the beacon
                will replace the entry with the lowest

    @param[in]  beacon       The network/cluster beacon
    @param[in]  max_beacons  The maximum beacons
*/
void Measurements_insert_beacon(const app_lib_state_beacon_rx_t * beacon)
{
    uint8_t i          = 0;
    uint8_t insert_idx = MAX_BEACONS;
    bool    match      = false;

    /** Searches for itself */
    for (i = 0; i < m_meas_table.num_beacons; i++)
    {
        /** if there is an entry present, use that index */
        if (m_meas_table.beacons[i].address == beacon->address)
        {
            insert_idx = i;
            match      = true;
            break;
        }
    }

    /** If there is no entry in the table for the given address, then simply
        append the beacon, otherwise replace the entry with the lowest minimum */
    if (!match)
    {
        if (m_meas_table.num_beacons == MAX_BEACONS)  // no space
        {
            update_min();

            if (beacon->rssi > m_meas_table.min_path_loss)
            {
                insert_idx = m_meas_table.min_index;
            }
        }
        else
        {
            if (beacon->address == m_node_addr)
            {
                insert_idx = MEASUREMENTS_INDEX_NODE;
                /** Default table size includes node & next_hop measurements */
            }
            else if (beacon->address == m_next_hop)
            {
                insert_idx = MEASUREMENTS_INDEX_NEXT_HOP;
                /** Default table size includes node & next_hop measurements */
            }
            else
            {
                insert_idx = m_meas_table.num_beacons;
                m_meas_table.num_beacons++;  // table size has to be increased
            }
        }
    }

    /** update the table */
    if (insert_idx < MAX_BEACONS)
    {
        m_meas_table.beacons[insert_idx].address      = beacon->address;
        m_meas_table.beacons[insert_idx].path_loss    = beacon->rssi - beacon->txpower;
        m_meas_table.beacons[insert_idx].is_connected = beacon->cost;
        m_meas_table.beacons[insert_idx].txpower      = beacon->txpower;
        m_meas_table.beacons[insert_idx].is_next_hop
            = beacon->address == m_next_hop ? true : false;
    }
}

/**
    @brief      Getter for number of beacons

    @return     Returns the number of beacons stored in the measurement table.
*/
uint8_t Measurements_get_num_beacon(void)
{
    return m_meas_table.num_beacons;
}

/**
 * \brief RSSI Compare function used by qsort
 *
 * \param a measurement_wm_beacon_t pointer
 * \param b measurement_wm_beacon_t pointer
 * \return int -1 if b.value > a.value, 0 if both equals, 1 if a.value > b.value
 */
static int compare_rssi(const void * a, const void * b)
{
    const measurement_wm_beacon_t *data1 = a, *data2 = b;
    return (data1->path_loss < data2->path_loss) - (data1->path_loss > data2->path_loss);
}

/**
 * \brief Orders beacons from highest to lowest RSSI, except for the node
 * itself and its next hop
 *
 */
void Measurements_order_beacons_by_rssi(void)
{
    qsort(m_meas_table.beacons + MEASUREMENTS_INDEX_MAX,
          m_meas_table.num_beacons - MEASUREMENTS_INDEX_MAX,
          sizeof(measurement_wm_beacon_t),
          compare_rssi);
}

/**
    @brief      Retrieves the size of the buffer.

    @return     The size of the useful payload.
*/
uint8_t Measurements_payload_length(void)
{
    m_payload.len = (uint8_t) (m_payload.ptr - (uint8_t *) &(m_payload.bytes));

    return m_payload.len;
}

/**
    @brief      Adds a path loss measurement to the payload buffer.
    @return     Pointer to the current edge of the payload.
*/
const uint8_t * Measurements_payload_add_beacon_data(void)
{
    uint8_t i        = 0;
    uint8_t len      = 0;
    uint8_t num_meas = m_meas_table.num_beacons;


    if (num_meas > 0)
    {
        /** copy data from internal tracking structure */
        for (i = 0; i < num_meas; i++)
        {
            len = sizeof(measurement_rss_data_t);
            Measurements_payload_copy((void *) &(m_meas_table.beacons[i]), len);
        }
    }
    return (const uint8_t *) m_payload.ptr;
}
