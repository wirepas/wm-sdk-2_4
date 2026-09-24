#ifndef MCU_H_
#define MCU_H_

#define NRF_STATIC_INLINE __STATIC_INLINE

#if defined(NRF52832_XXAA)
// All pin IDs for single GPIO peripheral
#define GPIO_PIN_ID_MIN 0
#define GPIO_PIN_ID_MAX 255
#elif defined(NRF52840_XXAA)
#undef NRF52
// All pin IDs for single GPIO peripheral
#define GPIO_PIN_ID_MIN 0
#define GPIO_PIN_ID_MAX 255
#elif defined(NRF52833_XXAA)
// All pin IDs for single GPIO peripheral
#define GPIO_PIN_ID_MIN 0
#define GPIO_PIN_ID_MAX 255
#undef NRF52
#elif defined(NRF9120_XXAA)
// All pin IDs for single GPIO peripheral
#define GPIO_PIN_ID_MIN 0
#define GPIO_PIN_ID_MAX 255
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
 * - NRF_GPIOTE20: 8 channels and 2 interrupts for GPIO port P1 (peripheral domain)
 *                 Pins 32 and above.
 * - NRF_GPIOTE30: 4 channels and 2 interrupts for GPIO port P0 (low power domain)
 *                 Pins 0 - 31.
 */
#define GPIO_PIN_ID_MIN    32
#define GPIO_PIN_ID_MAX    255
#define NRF_GPIO           NRF_P1
#define NRF_GPIOTE         NRF_GPIOTE20
#define GPIOTE_IRQn        GPIOTE20_IRQn

#define GPIO_PIN_ID_LP_MIN 0
#define GPIO_PIN_ID_LP_MAX 31
#define NRF_GPIO_LP        NRF_P0
#define NRF_GPIOTE_LP      NRF_GPIOTE30
#define GPIOTE_LP_IRQn     GPIOTE30_IRQn

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
