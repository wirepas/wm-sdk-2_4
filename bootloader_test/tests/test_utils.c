/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>

#include "test_utils.h"
#include "test.h"
#include "print.h"


bool search_ext_flash_area(const memory_area_services_t * mem_area_services,
                           bl_memory_area_id_t *          id)
{
    bool result                     = true;
    *id                             = BL_MEMORY_AREA_UNDEFINED;
    uint8_t               num_areas = MAX_TESTED_MEMORY_AREAS;
    bl_memory_area_id_t   areas[num_areas];
    bl_memory_area_info_t info;

    mem_area_services->getAreaList(areas, &num_areas);
    for (uint8_t i = 0; i < num_areas; ++i)
    {
        bl_interface_res_e bl_res
            = mem_area_services->getAreaInfo(areas[i], &info);

        if (bl_res != BL_RES_OK)
        {
            result = false;
            Print_printf("ERROR: can't get external flash info.\n");
        }
        else if (info.external_flash)
        {
            *id = areas[i];
            break;
        }
    }

    if (*id == BL_MEMORY_AREA_UNDEFINED)
    {
        Print_printf("INFO: There are no areas located in external flash.\n");
    }

    return result;
}
