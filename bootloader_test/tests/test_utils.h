/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef _TEST_UTILS_H_
#define _TEST_UTILS_H_

#include "../../bootloader_test/api/bl_interface.h"

bool search_ext_flash_area(const memory_area_services_t * mem_area_services,
                           bl_memory_area_id_t *          id);

#endif  // _TEST_UTILS_H_
