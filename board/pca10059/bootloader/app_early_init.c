#include "nrf52840.h"
#include "nrf52840_bitfields.h"
void app_early_init(void)
{
    // In test networks few pca10059 devices has had problems with
    // rare pin resets. Also brown-out or power-on resets might have
    // occured. For more robust and coherent environments,
    // all pca10059 devices shall use REGOUT0 3.0V (just like Nordic
    // SDK would set it) and pin reset shall be disabled.

    // Check if pin reset is disabled and REGOUT0 is 3.0V
    // REGOUT0 value 4 in three lowest bits  ==> 3.0V
    if ((NRF_UICR->PSELRESET[0] != 0xffffffff) ||
        (NRF_UICR->PSELRESET[1] != 0xffffffff) ||
        (NRF_UICR->REGOUT0 != (0xfffffff8 | UICR_REGOUT0_VOUT_3V0)))
    {
        // PSELRESET and REGOUT0 settings in UICR needs to be modified.

        // Preserve old values that are not going to be set
        uint32_t nrffw[13];
        uint32_t nrfhw[12];
        uint32_t customer[32];
        uint32_t approtect;
        uint32_t nfcpins;
        uint32_t debugctrl;

        for (uint8_t i=0;i<13;i++)
        {
            nrffw[i] = NRF_UICR->NRFFW[i];
        }
        for (uint8_t i=0;i<12;i++)
        {
            nrfhw[i] = NRF_UICR->NRFHW[i];
        }
        for (uint8_t i=0;i<32;i++)
        {
            customer[i] = NRF_UICR->CUSTOMER[i];
        }
        approtect = NRF_UICR->APPROTECT;
        nfcpins = NRF_UICR->NFCPINS;
        debugctrl = NRF_UICR->DEBUGCTRL;

        // Enable erasing
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }

        // Erase UICR
        NRF_NVMC->ERASEUICR = 0x00000001;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }

        // Write enabled
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }

        // Write back the copies
        for (uint8_t i=0;i<13;i++)
        {
            NRF_UICR->NRFFW[i] = nrffw[i];
        }
        for (uint8_t i=0;i<12;i++)
        {
            NRF_UICR->NRFHW[i] = nrfhw[i];
        }
        for (uint8_t i=0;i<32;i++)
        {
            NRF_UICR->CUSTOMER[i] = customer[i];
        }
        // Do not set reset pin!
        //NRF_UICR->PSELRESET[0] = 0xffffffff; Just erased, already 0xffffffff
        //NRF_UICR->PSELRESET[1] = 0xffffffff; Just erased, already 0xffffffff
        NRF_UICR->APPROTECT = approtect;
        NRF_UICR->NFCPINS = nfcpins;
        NRF_UICR->DEBUGCTRL = debugctrl;
        // Value 4 in three lowest bits of REGOUT0 means 3.0V
        NRF_UICR->REGOUT0 = (0xfffffff8 | UICR_REGOUT0_VOUT_3V0);

        // Read only
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }

        // System reset is required to get the settings in use
        NVIC_SystemReset();
    }
}
