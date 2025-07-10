/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Linker script for Wirepas Generic Bootloader Updater Tool on nRF54L15
 *
 * This file is for the 1524 kB RRAM, 256 kB RAM variant
 *
 * Non-volatile memory is split into:
 *  - Bootloader area (BOOTLOADER)
 *  - Wirepas Mesh stack executable code area (FLASH)
 *  - Bootloader updater entry point and configuration (ENTRYPOINT)
 *    - Overlapping with the beginning of FLASH area
 *  - Bootloader updater executable code (BL_UPDATER)
 *    - At the end of FLASH area
 *    - Also contains the compressed new bootloader
 *    - Also contains backup copy of the first erase block of FLASH area
 *
 * The Generic Bootloader Updater Tool may use all RAM above 1k. The first 1k
 * is reserved for the bootloader, so that calling the bootloader API works
 * until the old bootloader is erased.
 */

/* RRAM doesn't need to be erased so this can be set to any value that is
 * sufficient for bootloader updater. */
__erase_sector_size__ = 512;

MEMORY
{
    BOOTLOADER (rx)     : ORIGIN = 0x00000000,
                          LENGTH = 32K
    FLASH (rx)          : ORIGIN = 0x00000000 + 32K,
                          LENGTH = 240K
    ENTRYPOINT (rx)     : ORIGIN = 0x00000000 + 32K,
                          LENGTH = __erase_sector_size__
    BL_UPDATER (rx)     : ORIGIN = 0x00000000 + 32K + 240K - 24K,
                          LENGTH = 24K

    /* Leave the first 1K of RAM alone, so that the bootloader API is available */
    BL_STATIC (rwx)     : ORIGIN = 0x20000000,          LENGTH = 1K
    RAM (rwx)           : ORIGIN = 0x20000000 + 6K,     LENGTH = 250K
    INVALID (rwx)       : ORIGIN = 0xFFFFFFFF,          LENGTH = 0
}

INCLUDE gcc_bl_updater_common.ld
