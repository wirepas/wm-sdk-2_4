/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include "mcu.h"

/* For reading back cleared interrupt events in NRF52 */
__IO uint32_t                   EVENT_READBACK; /* declared in nrf.h */
