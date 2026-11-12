/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef EXTERNAL_FLASH_CFI_H_
#define EXTERNAL_FLASH_CFI_H_

#include <stdint.h>

#include "external_flash.h"


/**
 * \brief Supported flash devices
 */
enum flash_ids_e
{
    /** Unknown flash memory */
    UNKNOWN,

    /** Macronix MX25R1635F */
    MX25R1635F
};


/**
 * \brief Flash device identification
 */
struct ext_flash_id_t
{
    /** Flash ID */
    enum flash_ids_e flash_id;

    /** Manufacturer ID */
    uint8_t man_id;

    /** Memory type */
    uint8_t mem_type;

    /** Memory capacity */
    uint8_t mem_capacity;
};


/**
 * \brief   Get external flash memory ID.
 * \return  Pointer to flash memory ID
 */
const struct ext_flash_id_t * ext_flash_get_id(void);


/**
 * \brief   Get external flash memory information.
 * \return  Pointer to flash memory information
 */
const flash_info_t * ext_flash_get_info(void);

#endif  // EXTERNAL_FLASH_CFI_H_
