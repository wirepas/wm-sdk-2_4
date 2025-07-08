#ifndef MCU_H_
#define MCU_H_

#define NRF_STATIC_INLINE __STATIC_INLINE

#if defined(NRF52832_XXAA)
#elif defined(NRF52840_XXAA)
#undef NRF52
#elif defined(NRF52833_XXAA)
#undef NRF52
#elif defined(NRF9120_XXAA)
/** Define NRF_GPIO, NRF_GPIOTE and GPIOTE_IRQn for nrf91xx. */
#define NRF_GPIO    NRF_P0
#define NRF_GPIOTE  NRF_GPIOTE0
#define GPIOTE_IRQn GPIOTE0_IRQn

/** Define NRF_PPI for nrf91xx. */
#define NRF_PPI     NRF_DPPIC
#elif defined(NRF54L15_XXAA)
/** Define NRF_GPIO, NRF_GPIOTE and GPIOTE_IRQn for nRF54L15.
 *
 * nRF54L15 has two GPIOTE instances:
 * - NRF_GPIOTE20: 8 channels and 2 interrupts for GPIO port P1
 * - NRF_GPIOTE30: 4 channels and 2 interrupts for GPIO port P0
 *
 * NRF_GPIOTE20 is selected here which means that GPIO events will be available
 * only for pins from port P1.
 */
#define NRF_GPIO    NRF_P1
#define NRF_GPIOTE  NRF_GPIOTE20
#define GPIOTE_IRQn GPIOTE20_IRQn
#define GPIOTE_INTENCLR_PORT_Msk                                               \
    (NRFX_CONCAT(GPIOTE_INTENCLR, NRF_GPIOTE_IRQ_GROUP, _PORT0NONSECURE_Msk)   \
     | NRFX_CONCAT(GPIOTE_INTENCLR, NRF_GPIOTE_IRQ_GROUP, _PORT0SECURE_Msk))

/** Define NRF_PPI for nRF54L15. */
#define NRF_PPI NRF_DPPIC20
#else
#error NRF52832_XXAA, NRF52833_XXAA, NRF52840_XXAA, NRF9120_XXAA or \
       NRF54L15_XXAA must be defined
#endif

#include "nrfx.h"
#include "mdk/nrf.h"
#include "hal/nrf_gpio.h"
#include "hal/nrf_gpiote.h"
#include "hal/nrf_uarte.h"
#include "hal/nrf_timer.h"

extern __IO uint32_t EVENT_READBACK;

#endif /* MCU_H_ */
