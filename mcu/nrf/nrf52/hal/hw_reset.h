/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef HW_RESET_NRF52_H_
#define HW_RESET_NRF52_H_

#if MCU_SUB == 840
   #define RESET_PIN 18
#elif MCU_SUB == 833
    #define RESET_PIN 18
#elif MCU_SUB == 832
    #define RESET_PIN 21
#else
    #error "Unsupported MCU_SUB value"
#endif

void Config_Reset_Pins(void);

#endif /* HW_RESET_NRF52_H_ */
