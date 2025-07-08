/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef UNIQUEID_H
#define UNIQUEID_H

#include "mcu.h"


static inline uint32_t getUniqueId()
{
    return NRF_FICR->INFO.DEVICEID[0];
}

#endif  // UNIQUEID_H
