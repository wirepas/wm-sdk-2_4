/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include "hw_reset.h"
#include "mcu.h"

void Config_Reset_Pins(void)
{
    if (((NRF_UICR->PSELRESET[0] & UICR_PSELRESET_CONNECT_Msk) != (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos)) ||
        ((NRF_UICR->PSELRESET[1] & UICR_PSELRESET_CONNECT_Msk) != (UICR_PSELRESET_CONNECT_Connected << UICR_PSELRESET_CONNECT_Pos)))
    {
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }
        NRF_UICR->PSELRESET[0] = RESET_PIN;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }
        NRF_UICR->PSELRESET[1] = RESET_PIN;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }
        NVIC_SystemReset();
    }
}
