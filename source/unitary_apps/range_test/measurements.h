/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * @file  measurements.h
 * @brief Header for measurements.c
*/

#ifndef RANGE_MEASUREMENTS_H
#define RANGE_MEASUREMENTS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_PAYLOAD 102  // 2.4 profile - limited by internal memory
#define MAX_BEACONS 14   // limited by internal memory

typedef uint16_t   measurement_header_sequence_t;
typedef uint8_t    positioning_header_length_t;
typedef int16_t    measurement_payload_path_loss_t;
typedef app_addr_t measurement_payload_addr_t;

/**
    @brief stores the measurement data being sent out towards a sink.
*/
typedef struct __attribute__((__packed__))
{
    measurement_payload_addr_t      address;
    measurement_payload_path_loss_t value;
    uint8_t                         cost;
    bool                            is_next_hop;
} measurement_rss_data_t;


/**
    @brief contains the WM beacon data obtained by the device.
*/
typedef struct __attribute__((__packed__))
{
    app_addr_t address;
    int16_t    path_loss;
    uint8_t    is_connected;
    int8_t     txpower;
    bool       is_next_hop;
} measurement_wm_beacon_t;

/**
    @brief buffer to help prepare the measurement payload.
*/
typedef struct
{
    uint8_t   bytes[MAX_PAYLOAD];
    uint8_t   len;
    uint8_t * ptr;
} measurement_payload_t;

/**
    @brief stores the beacon data.
*/
typedef struct
{
    measurement_wm_beacon_t beacons[MAX_BEACONS];
    uint8_t                 num_beacons;
    uint8_t                 min_index;
    int8_t                  min_path_loss;
} measurement_table_t;

void    Measurements_init(void);
void    Measurements_table_reset(void);
uint8_t Measurements_get_num_beacon(void);
void    Measurements_clean_beacon(uint32_t older_than, uint8_t max_beacons);
void    Measurements_insert_beacon(const app_lib_state_beacon_rx_t * beacon);
void    Measurements_order_beacons_by_rssi(void);

const uint8_t * Measurements_payload_init(void);
uint8_t         Measurements_payload_length(void);
const uint8_t * Measurements_payload_add_beacon_data(void);

#endif /*RANGE_MEASUREMENTS_H*/
