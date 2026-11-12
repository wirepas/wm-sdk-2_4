/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef NRF_I2C_DEFS
#define NRF_I2C_DEFS

#if defined(USE_I2C0)
#define I2C_IRQn TWIM20_IRQn
#define I2C_DEV  NRF_TWIM20
#elif defined(USE_I2C1)
#define I2C_IRQn TWIM21_IRQn
#define I2C_DEV  NRF_TWIM21
#elif defined(USE_I2C2)
#define I2C_IRQn TWIM22_IRQn
#define I2C_DEV  NRF_TWIM22
#else
#error USE_I2Cx (x=0-2) must be defined
#endif

/* Define TASKS for the nRF54 series. */
#define I2C_DEV_TASKS_STARTTX (I2C_DEV->TASKS_DMA.TX.START)
#define I2C_DEV_TASKS_STARTRX (I2C_DEV->TASKS_DMA.RX.START)

/* Define RX/TX buffer registers for the nRF54 series. */
#define I2C_DEV_TX_PTR    (I2C_DEV->DMA.TX.PTR)
#define I2C_DEV_TX_MAXCNT (I2C_DEV->DMA.TX.MAXCNT)
#define I2C_DEV_RX_PTR    (I2C_DEV->DMA.RX.PTR)
#define I2C_DEV_RX_MAXCNT (I2C_DEV->DMA.RX.MAXCNT)

/* Define disabled interrupts for the nRF54 series. */
#define I2C_DEV_INTENCLR_ALL                                                   \
    (TWIM_INTENCLR_STOPPED_Msk | TWIM_INTENCLR_ERROR_Msk                       \
     | TWIM_INTENCLR_SUSPENDED_Msk | TWIM_INTENCLR_LASTRX_Msk                  \
     | TWIM_INTENCLR_LASTTX_Msk | TWIM_INTENCLR_DMARXEND_Msk                   \
     | TWIM_INTENCLR_DMARXREADY_Msk | TWIM_INTENCLR_DMARXBUSERROR_Msk          \
     | TWIM_INTENCLR_DMARXMATCH0_Msk | TWIM_INTENCLR_DMARXMATCH1_Msk           \
     | TWIM_INTENCLR_DMARXMATCH2_Msk | TWIM_INTENCLR_DMARXMATCH3_Msk           \
     | TWIM_INTENCLR_DMATXEND_Msk | TWIM_INTENCLR_DMATXREADY_Msk               \
     | TWIM_INTENCLR_DMATXBUSERROR_Msk)

/* Define the LASTTX_STARTRX shortcut for the nRF54 series. */
#define TWIM_SHORTS_LASTTX_STARTRX_Msk (TWIM_SHORTS_LASTTX_DMA_RX_START_Msk)

#endif  // NRF_I2C_DEFS
