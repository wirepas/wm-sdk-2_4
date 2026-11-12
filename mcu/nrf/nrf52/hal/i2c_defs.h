#ifndef NRF_I2C_DEFS
#define NRF_I2C_DEFS

#if defined(USE_I2C1)
#define I2C_IRQn SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn
#define I2C_DEV  NRF_TWIM1
#elif defined(USE_I2C0)
#define I2C_IRQn SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn
#define I2C_DEV  NRF_TWIM0
#else
#error USE_I2C0 or USE_I2C1 must be defined
#endif

/* Define TASKS for the nRF52 series. */
#define I2C_DEV_TASKS_STARTTX (I2C_DEV->TASKS_STARTTX)
#define I2C_DEV_TASKS_STARTRX (I2C_DEV->TASKS_STARTRX)

/* Define RX/TX buffer registers for the nRF52 series. */
#define I2C_DEV_TX_PTR    (I2C_DEV->TXD.PTR)
#define I2C_DEV_TX_MAXCNT (I2C_DEV->TXD.MAXCNT)
#define I2C_DEV_RX_PTR    (I2C_DEV->RXD.PTR)
#define I2C_DEV_RX_MAXCNT (I2C_DEV->RXD.MAXCNT)

/* Define disabled interrupts for the nRF52 series. */
#define I2C_DEV_INTENCLR_ALL                                                   \
    (TWIM_INTENCLR_STOPPED_Msk | TWIM_INTENCLR_ERROR_Msk                       \
     | TWIM_INTENCLR_RXSTARTED_Msk | TWIM_INTENCLR_TXSTARTED_Msk               \
     | TWIM_INTENCLR_LASTRX_Msk | TWIM_INTENCLR_LASTTX_Msk)

#endif  // NRF_I2C_DEFS
