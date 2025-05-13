/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Linker script for Wirepas Generic Bootloader Updater Tool on EFR32xG21xxxxF1024
 *
 * This file is for the 1024 kB Flash, 96 kB RAM variant
 *
 * Flash is split into:
 *  - Bootloader area (BOOTLOADER)
 *  - Wirepas Mesh stack executable code area (FLASH)
 *  - Bootloader updater entry point and configuration (ENTRYPOINT)
 *    - Overlapping with first erase block of FLASH area
 *  - Bootloader updater executable code (BL_UPDATER)
 *    - At the end of FLASH area
 *    - Also contains the compressed new bootloader
 *    - Also contains backup copy of the first erase block of FLASH area
 *
 * The Generic Bootloader Updater Tool may use all RAM above 1k. The first 1k
 * is reserved for the bootloader, so that calling the bootloader API works
 * until the old bootloader is erased.
 */

__erase_sector_size__ = 8K;  /* Must match the eraseblock value in nnn_wp.ini */

MEMORY
{
    BOOTLOADER (rx)     : ORIGIN = 0x00000000,
                          LENGTH = 32K
    FLASH (rx)          : ORIGIN = 0x00000000 + 32K,
                          LENGTH = 224K
    ENTRYPOINT (rx)     : ORIGIN = 0x00000000 + 32K,
                          LENGTH = __erase_sector_size__
    BL_UPDATER (rx)     : ORIGIN = 0x00000000 + 32K + 224K - 24K,
                          LENGTH = 24K

    /* Leave the first 1K of RAM alone, so that the bootloader API is available */
    BL_STATIC (rwx)     : ORIGIN = 0x20000000 + 24,     LENGTH = 1K
    RAM (rwx)           : ORIGIN = 0x20001500,          LENGTH = 96K - 0x1500
    INVALID (rwx)       : ORIGIN = 0xFFFFFFFF,          LENGTH = 0
}

INCLUDE gcc_bl_updater_common.ld
