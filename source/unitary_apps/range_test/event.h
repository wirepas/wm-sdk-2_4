/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * @file    events.h
 * @brief   Header file for the application events.
 */

#ifndef RANGE_TEST_EVENTS_H
#define RANGE_TEST_EVENTS_H

#include "api.h"

void Event_beacon_reception(const app_lib_state_beacon_rx_t * beacon);
void Event_network_scan_end();

#endif
