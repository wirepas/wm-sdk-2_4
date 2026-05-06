/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include "board.h"
#include "power.h"

#include "mcu.h"
#include "mdk/nrf_erratas.h"

void Power_enableDCDC()
{
#if BOARD_HW_DCDC
    NRF_REGULATORS->VREGMAIN.DCDCEN = REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled
                                      << REGULATORS_VREGMAIN_DCDCEN_VAL_Pos;
#endif
}
