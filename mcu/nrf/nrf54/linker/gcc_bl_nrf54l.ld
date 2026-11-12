/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Linker script for Wirepas Mesh bootloader on nRF54L
 *
 * This file is for all nRF54L memory variants:
 *  - nRF54L05 the  500 kB RRAM,  96 kB RAM variant
 *  - nRF54L10 the 1012 kB RRAM, 192 kB RAM variant
 *  - nRF54L15 the 1524 kB RRAM, 256 kB RAM variant
 *
 * Non-volatile memory is split into:
 *  - Bootloader executable code (BOOTLOADER)
 *  - Bootloader config: memory areas, auth. and encryption keys (BLCONFIG)
 *
 * RAM is split into:
 *  - RAM for bootloader variables, to support runtime services (BL_STATIC)
 *  - Next 5kB of RAM must be retained over boot and not modified by bootloader
 *  - Rest of the RAM the bootloader can use for variables during booting (RAM)
 */

MEMORY
{
    BOOTLOADER (rx)   : ORIGIN = 0x00000000,                        LENGTH = __bl_length__ - 1K
    BLCONFIG (rx)     : ORIGIN = 0x00000000 + __bl_length__ - 1K,   LENGTH = 1K
    BL_STATIC (rwx)   : ORIGIN = 0x20000000,                        LENGTH = 1K
    RAM (rwx)         : ORIGIN = 0x20000000 + 6K,                   LENGTH = 250K
    INVALID (rwx)     : ORIGIN = 0xFFFFFFFF,                        LENGTH = 0
}

INCLUDE gcc_bl_common.ld
