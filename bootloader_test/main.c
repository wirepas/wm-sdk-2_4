/* Copyright 2018 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <string.h>

#include "../bootloader_test/api/bl_interface.h"
#include "../bootloader_test/print/print.h"
#include "../bootloader_test/tests/test.h"
#include "../bootloader_test/timing/timing.h"

#include "mcu.h"

#if defined(_SILICON_LABS_32B_SERIES)
#include "em_chip.h"
#endif  // _SILICON_LABS_32B_SERIES

/** Addresses determined by the linker */
extern unsigned int __data_src_start__;
extern unsigned int __data_start__;
extern unsigned int __data_end__;
extern unsigned int __bss_start__;
extern unsigned int __bss_end__;

/* We need to reserve some space for the application header. It is not used but
 * otherwise genScratchpad.py would overwrite this area.
 */
const uint32_t info_hdr[8] __attribute__(( section (".app_header")));


/**
 * \brief   Enable reset button on nRF52 DKs.
 */
static inline void Reset_init(void)
{
#if defined(NRF52_SERIES)
#if defined(NRF52832_XXAA)
    /* On nRF52832-DK reset button is connected to P0.21. */
    uint32_t reset_pin = 21;
#else
    /* On nRF52833-DK and nRF52840-DK reset button is connected to P0.18. */
    uint32_t reset_pin = 18;
#endif  // defined(NRF52832_XXAA)

    if ((NRF_UICR->PSELRESET[0] & UICR_PSELRESET_CONNECT_Msk)
        != (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos))
    {
        uint32_t pselreset_value
            = (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos)
              | reset_pin;

        NRF_NVMC->CONFIG       = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
        NRF_UICR->PSELRESET[0] = pselreset_value;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }

        NRF_UICR->PSELRESET[1] = pselreset_value;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
        NVIC_SystemReset();
    }
#endif  // defined(NRF52_SERIES)
}


/**
 * \brief   The bootloader test application.
 */
void bootloader_test(bl_interface_t * interface)
{
    unsigned int * src, * dst;
    bool final_res = true;

    /* Copy data from flash to RAM */
    for(src = &__data_src_start__,
        dst = &__data_start__;
        dst != &__data_end__;)
    {
        *dst++ = *src++;
    }

    /* Initialize the .bss section */
    for(dst = &__bss_start__; dst != &__bss_end__;)
    {
        *dst++ = 0;
    }

    Reset_init();
    Timing_init();
    Print_init();

    Print_printf("\n\n #######################################\n");
    Print_printf(    " #                                     #\n");
    Print_printf(    " #      Starting bootloader tests      #\n");
    Print_printf(    " #                                     #\n");
    Print_printf(    " #######################################\n\n");

    Print_printf("Bootloader version is %d\n", interface->version);

    final_res &= Tests_info(interface);
    final_res &= Tests_areas(interface);
    final_res &= Tests_timings(interface);

    Print_printf("\n\n #######################################\n");
    Print_printf(    " #                                     #\n");
    Print_printf(    " #         Final result is %s        #\n",
                                                final_res ? "PASS" : "FAIL");
    Print_printf(    " #                                     #\n");
    Print_printf(    " #######################################\n\n");

    while(1);

}


/**
 * \brief   Entrypoint from bootloader
 */
void __attribute__ ((noreturn, section (".entrypoint")))
                                        entrypoint(bl_interface_t * interface)
{
    bootloader_test(interface);

    while(1);
}
