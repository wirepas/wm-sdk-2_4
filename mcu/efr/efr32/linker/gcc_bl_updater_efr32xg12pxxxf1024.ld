/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Linker script for Wirepas Generic Bootloader Updater Tool on EFR32xG12PxxxF1024
 *
 * This file is for the 1024 kB Flash, 128 kB RAM variant
 *
 * Non-volatile memory is split into:
 *  - Bootloader area (BOOTLOADER)
 *    - Can be 16 or 32 kB in size
 *  - First 16 kB of the Wirepas Mesh stack executable code area (KEEPOUT)
 *    - Optional, not used by the second stage loader
 *    - Protects the bootloader updater from accidentally erasing itself when
 *      updating a 16 kB bootloader to a 32 kB one
 *  - Wirepas Mesh stack executable code area (MESH_STACK), containing:
 *    - The bootloader updater and configuration
 *    - The compressed new bootloader
 *    - A new scratchpad with the Wirepas Mesh stack
 *
 * The Generic Bootloader Updater Tool may use all RAM above 1k. The first 1k
 * is reserved for the bootloader, so that calling the bootloader API works
 * until the old bootloader is erased.
 */

__erase_sector_size__ = 2K;  /* Must match the eraseblock value in nnn_wp.ini */

MEMORY
{
    BOOTLOADER (rx)     : ORIGIN = __bootloader_area_addr__,
                          LENGTH = __bootloader_area_length__
    KEEPOUT (rx)        : ORIGIN = __stack_area_addr__,
                          LENGTH = __keepout_length__
    MESH_STACK (rx)     : ORIGIN = __stack_area_addr__ + __keepout_length__,
                          LENGTH = __stack_area_length__ - __keepout_length__

    /* Leave the first 1K of RAM alone, so that the bootloader API is available */
    BL_STATIC (rwx)     : ORIGIN = 0x20000000,          LENGTH = 1K
    RAM (rwx)           : ORIGIN = 0x20000000 + 1K,     LENGTH = 127K - 24
    INVALID (rwx)       : ORIGIN = 0xFFFFFFFF,          LENGTH = 0
}

INCLUDE gcc_bl_updater_common.ld
