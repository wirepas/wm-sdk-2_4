/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "external_flash.h"
#include "external_flash_cfi.h"
#include "external_flash_platform.h"

/** Overread character that is sent when data is being read but there is no data
 *  to send */
#define SPI_ORC (0xFF)

/** Time to wait before device is ready for access after applying power */
#define TIMING_POWERDOWN_MAX_MS (10U)

/** Time to wait until device is released from power-down mode */
#define TIMING_POWERON_MAX_MS (30U)

/** Bit position of high performance mode in "Configuration Register - 2" in
 *  Macronix flash devices */
#define MX25_HIGH_PERF_MODE_SHIFT 1U

/** When defined write and erase tests are performed at the end of flash
 *  initialization. Running these tests will alter flash contents. */
// #define EXT_FLASH_CONTENT_ALTERING_TEST

/**
 * \brief Flash status register
 */
enum
{
    EXT_FLASH_STATUS_WIP = 0x01,  // Write In Progress = Busy
    EXT_FLASH_STATUS_WEL = 0x02   // Write Enable Latch
} ext_flash_status_t;

/**
 * \brief Flash commands
 */
enum
{
    EXT_FLASH_CMD_WRITE_STATUS        = 0x01,
    EXT_FLASH_CMD_PROGRAM_PAGE        = 0x02,
    EXT_FLASH_CMD_READ_ARRAY          = 0x03,
    EXT_FLASH_CMD_READ_STATUS         = 0x05,
    EXT_FLASH_CMD_WRITE_ENABLE        = 0x06,
    EXT_FLASH_CMD_BLOCK_ERASE_32K     = 0x52,
    EXT_FLASH_CMD_READ_IDENTIFICATION = 0x9F,
    EXT_FLASH_CMD_RES                 = 0xAB
} ext_flash_cmd_t;


/**
 * \brief   Perform erase and write tests.
 * \note    These tests will alter flash contents!
 * \return  True if tests passed
 */
static bool ext_flash_test(void);


/**
 * \brief   Set the address of the current flash command byte buffer. Addressing
 *          is done through a 3-byte address mode for NOR flash with capacity up
 *          to 128 M-bit.
 * \param   command_p
 *          Pointer to a byte buffer that stores the 3-byte address
 * \param   address
 *          Address of flash operation
 */
static inline void set_address(uint8_t * command_p, uint32_t address)
{
    *command_p++ = ((address >> 16) & 0xFF);
    *command_p++ = ((address >> 8) & 0xFF);
    *command_p   = (address & 0xFF);
}


/**
 * \brief   Enable flash write operation by sending the Write Enable (WREN)
 *          command to set the Write Enable Latch (WEL) bit.
 * \return  True if the WEL bit was set successfully, false otherwise
 */
static bool ext_flash_write_enable()
{
    uint8_t reply[2] = { 0 };

    // Send Write Enable command
    ext_flash_set_cs_active();
    ext_flash_spi_transfer(EXT_FLASH_CMD_WRITE_ENABLE);
    ext_flash_set_cs_inactive();

    // Wait while WIP bit is set
    while (externalFlash_isBusy())
    {
    }

    // Send Read Status command
    ext_flash_set_cs_active();
    ext_flash_spi_transfer(EXT_FLASH_CMD_READ_STATUS);
    reply[0] = ext_flash_spi_transfer(SPI_ORC);
    reply[1] = ext_flash_spi_transfer(SPI_ORC);
    ext_flash_set_cs_inactive();

    // Check that the WEL bit is set
    return ((reply[1] & EXT_FLASH_STATUS_WEL) == EXT_FLASH_STATUS_WEL);
}


/**
 * \brief   Set flash to high performance mode.
 * \return  True if high performance mode was enabled successfully, false
 *          otherwise
 */
static bool ext_flash_set_high_performance_mode()
{
    // Enable write access
    if (!ext_flash_write_enable())
    {
        return false;
    }

    // Send Write Status command to enable high performance mode
    ext_flash_set_cs_active();
    ext_flash_spi_transfer(EXT_FLASH_CMD_WRITE_STATUS);

    // Status register
    ext_flash_spi_transfer(0);

    // Configuration register-1
    ext_flash_spi_transfer(0);

    // Configuration register-2
    ext_flash_spi_transfer(1 << MX25_HIGH_PERF_MODE_SHIFT);
    ext_flash_set_cs_inactive();

    // Wait while WIP is set
    while (externalFlash_isBusy())
    {
    }

    return true;
}


extFlash_res_e externalFlash_init(void)
{
    ext_flash_init_platform();

    ext_flash_debug_init();

    // Ensure the device is ready to access after applying power
    // We delay even if shutdown control isn't used to play it safe
    // since we don't know how quickly init may be called after boot
    active_wait_ms(TIMING_POWERDOWN_MAX_MS);

    // Release the chip from powerdown mode by sending the Read Electronic
    // Signature command
    ext_flash_set_cs_active();
    ext_flash_spi_transfer(EXT_FLASH_CMD_RES);
    ext_flash_set_cs_inactive();

    active_wait_ms(TIMING_POWERON_MAX_MS);

    // Sending Read Identification command
    ext_flash_set_cs_active();
    ext_flash_spi_transfer(EXT_FLASH_CMD_READ_IDENTIFICATION);
    uint8_t man_id       = ext_flash_spi_transfer(SPI_ORC);
    uint8_t mem_type     = ext_flash_spi_transfer(SPI_ORC);
    uint8_t mem_capacity = ext_flash_spi_transfer(SPI_ORC);
    ext_flash_set_cs_inactive();

    // Verify that the flash is a known one
    bool chip_detected
        = ((ext_flash_get_id()->man_id == man_id)
           && (ext_flash_get_id()->mem_type == mem_type)
           && (ext_flash_get_id()->mem_capacity == mem_capacity));

    bool init_ok = false;
    if (chip_detected)
    {
        // Flash chip is detected
        // Set it to high performance mode
        if (ext_flash_set_high_performance_mode())
        {
            init_ok = ext_flash_test();
        }
    }

    extFlash_res_e result;
    if (init_ok)
    {
        result = EXTFLASH_RES_OK;
        ext_flash_debug_success();
    }
    else
    {
        result = EXTFLASH_RES_ERROR;
        ext_flash_debug_fail();
    }

    return result;
}


extFlash_res_e externalFlash_startRead(void * to, const void * from,
                                       size_t to_read_nb)
{
    // Check if flash is busy
    if (externalFlash_isBusy())
    {
        return EXTFLASH_RES_BUSY;
    }

    // Check parameters
    uint32_t read_addr = (uint32_t) from;
    if ((read_addr + to_read_nb) > ext_flash_get_info()->flash_size)
    {
        return EXTFLASH_RES_PARAM;
    }

    uint8_t read_cmd[4];
    read_cmd[0] = EXT_FLASH_CMD_READ_ARRAY;
    set_address(&read_cmd[1], read_addr);

    // Send Read Data Bytes command
    ext_flash_set_cs_active();
    for (uint8_t i = 0; i < sizeof(read_cmd); i++)
    {
        ext_flash_spi_transfer(read_cmd[i]);
    }

    // Read the data
    uint8_t * read_buf = to;
    while (to_read_nb > 0)
    {
        *read_buf++ = ext_flash_spi_transfer(SPI_ORC);
        to_read_nb--;
    }

    ext_flash_set_cs_inactive();

    return EXTFLASH_RES_OK;
}


extFlash_res_e externalFlash_startWrite(void * to, const void * from,
                                        size_t to_write_nb)
{
    // Check if flash is busy
    if (externalFlash_isBusy())
    {
        return EXTFLASH_RES_BUSY;
    }

    /* Check that write do not cross page boundary */
    uint32_t write_addr = (uint32_t) to;
    uint32_t next_page
        = (write_addr
           & (0xFFFFFFFF - (ext_flash_get_info()->write_page_size - 1)))
          + ext_flash_get_info()->write_page_size;

    if ((write_addr + to_write_nb) > next_page)
    {
        return EXTFLASH_RES_PARAM;
    }

    if ((write_addr + to_write_nb) > ext_flash_get_info()->flash_size)
    {
        return EXTFLASH_RES_PARAM;
    }

    if (to_write_nb > ext_flash_get_info()->write_page_size)
    {
        return EXTFLASH_RES_PARAM;
    }

    // Enable flash write
    if (!ext_flash_write_enable())
    {
        return EXTFLASH_RES_ERROR;
    }

    // Send Page Program command
    uint8_t write_cmd[4];
    write_cmd[0] = EXT_FLASH_CMD_PROGRAM_PAGE;
    set_address(&write_cmd[1], write_addr);

    ext_flash_set_cs_active();

    for (uint8_t i = 0; i < sizeof(write_cmd); i++)
    {
        ext_flash_spi_transfer(write_cmd[i]);
    }

    // Send the data
    const uint8_t * source_buf = from;
    while (to_write_nb > 0)
    {
        ext_flash_spi_transfer(*source_buf++);
        to_write_nb--;
    }

    ext_flash_set_cs_inactive();

    return EXTFLASH_RES_OK;
}


bool externalFlash_isBusy(void)
{
    uint8_t reply[2] = { 0 };

    // Send Read Status command
    ext_flash_set_cs_active();
    ext_flash_spi_transfer(EXT_FLASH_CMD_READ_STATUS);
    reply[0] = ext_flash_spi_transfer(SPI_ORC);
    reply[1] = ext_flash_spi_transfer(SPI_ORC);
    ext_flash_set_cs_inactive();

    // Check if the WIP bit is set
    return ((reply[1] & EXT_FLASH_STATUS_WIP) == EXT_FLASH_STATUS_WIP);
}


extFlash_res_e externalFlash_startErase(size_t * sector_base,
                                        size_t * number_of_sector)
{
    // Check if flash is busy
    if (externalFlash_isBusy())
    {
        return EXTFLASH_RES_BUSY;
    }

    // Check parameters
    size_t addr = *sector_base;
    if ((addr % ext_flash_get_info()->erase_sector_size) != 0)
    {
        return EXTFLASH_RES_PARAM;
    }

    if ((addr + ext_flash_get_info()->erase_sector_size)
        > ext_flash_get_info()->flash_size)
    {
        return EXTFLASH_RES_PARAM;
    }

    // Enable Write
    if (!ext_flash_write_enable())
    {
        return EXTFLASH_RES_ERROR;
    }

    // Send Block Erase command
    uint8_t cmd[4];
    cmd[0] = EXT_FLASH_CMD_BLOCK_ERASE_32K;
    set_address(&cmd[1], addr);

    ext_flash_set_cs_active();

    for (uint8_t i = 0; i < sizeof(cmd); i++)
    {
        ext_flash_spi_transfer(cmd[i]);
    }

    ext_flash_set_cs_inactive();

    // Increment the base address and decrement number of block to erase
    *sector_base += ext_flash_get_info()->erase_sector_size;
    *number_of_sector -= 1;

    return EXTFLASH_RES_OK;
}


extFlash_res_e externalFlash_getInfo(flash_info_t * info)
{
    // Copy flash characteristics
    memcpy(info, ext_flash_get_info(), sizeof(flash_info_t));
    return EXTFLASH_RES_OK;
}


#if defined(EXT_FLASH_CONTENT_ALTERING_TEST)

static bool ext_flash_test_erase()
{
    size_t  addr    = 0;
    size_t  sectors = 1;
    uint8_t buff[16];

    // Make sure flash is ready
    while (externalFlash_isBusy())
    {
    }

    // Erase first sector
    if (externalFlash_startErase(&addr, &sectors) != EXTFLASH_RES_OK)
    {
        return false;
    }

    // Make sure flash is ready
    while (externalFlash_isBusy())
    {
    }

    if (sectors != 0)
    {
        return false;
    }
    if (addr != 32768)
    {
        return false;
    }

    // Read that first 16 bytes and check those are erased
    if (externalFlash_startRead(buff, 0, 16) != EXTFLASH_RES_OK)
    {
        return false;
    }

    for (uint8_t t = 0; t < 16; ++t)
    {
        if (buff[t] != 0xff)
        {
            return false;
        }
    }

    return true;
}


static bool ext_flash_test_write()
{
    uint8_t buff[16];

    // Create test data to buffer
    for (uint8_t t = 0; t < 16; ++t)
    {
        buff[t] = t ^ 0xaa;
    }

    // Write test data to beginning of external flash
    if (externalFlash_startWrite(0, buff, 16) != EXTFLASH_RES_OK)
    {
        return false;
    }

    // Make sure flash is ready
    while (externalFlash_isBusy())
    {
    }

    // Read that first 16 bytes and check those are the same as test data
    if (externalFlash_startRead(buff, 0, 16) != EXTFLASH_RES_OK)
    {
        return false;
    }

    for (uint8_t t = 0; t < 16; ++t)
    {
        if (buff[t] != (t ^ 0xaa))
        {
            return false;
        }
    }
    return true;
}


static bool ext_flash_test()
{
    return (ext_flash_test_erase() && ext_flash_test_write());
}

#else

static bool ext_flash_test()
{
    return true;
}

#endif  // defined(EXT_FLASH_CONTENT_ALTERING_TEST)
