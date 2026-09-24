/* Copyright 2018 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * \file
 * \brief   Simple minimal SPI master driver
 *          It only manages one SPI instance at a time
 */
#ifndef SPI_H_
#define SPI_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * \brief Different SPI modes
 * \note The enum values are handled differently in Silabs and Nordic chips:
 *       - In Nordic chips, this controls the polarity on active state.
 *       - In Silabs chips, this controls the polarity on idle state.<br><br>
 *       Hence the same enum value used in Nordic/Silabs chips results in
 *       inverse polarity.
 */
typedef enum
{
    SPI_MODE_LOW_FIRST,     //< Low Polarity, leading edge
    SPI_MODE_LOW_SECOND,    //< Low Polarity, trailing edge
    SPI_MODE_HIGH_FIRST,    //< High Polarity, leading edge
    SPI_MODE_HIGH_SECOND    //< High Polarity, trailing edge
} spi_mode_e;

/** Different bit orders */
typedef enum
{
    SPI_ORDER_MSB,  //< Most Significant Bit first
    SPI_ORDER_LSB   //< Less Significant Bit first
} spi_bit_order_e;

/** Structure to configure the SPI */
typedef struct
{
    uint32_t        clock;      //< SPI speed in Hz
    spi_mode_e      mode;       //< SPI mode of operation
    spi_bit_order_e bit_order;  //< SPI bit order
} spi_conf_t;

/** Structure to describe a SPI transfer */
typedef struct
{
    uint8_t *   write_ptr;  //< Pointer to bytes to write (Must be NULL for pure read)
    size_t      write_size; //< Number of bytes to write (Must be 0 for pure read)
    uint8_t *   read_ptr;   //< Pointer to store bytes to read (Must be NULL for pure write)
    size_t      read_size;  //< Number of bytes to read (Must be 0 for pure write)
    uint32_t    custom;     //< Custom param (can be used to implement state machine)
} spi_xfer_t;

/** Return codes of SPI functions */
typedef enum
{
    SPI_RES_OK,
    SPI_RES_INVALID_CONFIG,
    SPI_RES_INVALID_XFER,
    SPI_RES_NOT_INITIALIZED,
    SPI_RES_ALREADY_INITIALIZED,
    SPI_RES_BUSY,
    SPI_RES_BLOCKING_NOT_AVAILABLE,
    SPI_RES_ONLY_BLOCKING_AVAILABLE
} spi_res_e;

/** User callback when transfer is done */
typedef void (*spi_on_transfer_done_cb_f)(spi_res_e res,
                                      spi_xfer_t * xfer_p);

/**
 * \brief   Initialize SPI module
 * \param   conf_p
 *          Pointer to an spi configuration
 * \return  Return code of operation
 */
spi_res_e SPI_init(spi_conf_t * conf_p);

/**
 * \brief   Close an already initialized SPI module
 * \return  Return code of operation
 */
spi_res_e SPI_close();

/**
 * \brief   Enable or disable USART
 * \param   xfer_p
 *          Pointer to the transfer description
 * \param   cb
 *          Callback to call at end of transfer (Can be NULL for a blocking call)
 * \return  Return code of operation
 * \note    Application is in charge to select the write slave with chip select signal
 */
spi_res_e SPI_transfer(spi_xfer_t * xfer_p,
                       spi_on_transfer_done_cb_f cb);


#endif /* SPI_H_ */
