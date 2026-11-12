/* Copyright 2019 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include "board_init.h"
#include "board.h"
#include "power.h"
#include "radio.h"

#if defined(NRF52) && defined(USE_NRF52_HWRESET)
    #include "hw_reset.h"
#endif

/* Declaration of weak a custom board initialization that
 * can be overwritten for each board under board/<board_name>/board_custom_init.c
 */
void Board_custom_init(void) __attribute__((weak));

void Board_init(void)
{
    Board_custom_init();

/* Needed for nodes flashed prior SDK v1.2 (with bootloader < v7). */
#if defined(NRF52)
    Power_enableDCDC();
#endif

#if defined(NRF52) && defined(USE_NRF52_HWRESET)
    Config_Reset_Pins();
#endif

    Radio_init();
}
