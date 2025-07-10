/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Linker script for Wirepas Mesh application on nRF54L05
 *
 * This file is for the 510 kB RRAM, 96 kB RAM variant
 *
 * Non-volatile memory areas:
 *  - Application executable code (RRAM)
 *      - By default, 256 kB of non-volatile memory is allocated for
 *        the application
 *      - It's possible to allocate more space for the application, but this
 *        will reduce the available space for the stored scratchpad
 *      - The scratchpad is typically stored in the internal non-volatile
 *        memory and occupies the same memory area as the application
 *      - If the application is too large, there may not be sufficient
 *        space for Over-The-Air Programming (OTAP)
 *      - For more details, refer to the OTAP documentation
 *
 * RAM memory areas:
 *  - RAM for application static variables (RAM)
 *      - The application is allocated 48 kB of RAM
 */

MEMORY
{
  /* See the note above, before changing RRAM length (the FLASH block below) */
  FLASH (rx)      : ORIGIN = 0x00044000, LENGTH = 256K
  RAM (rwx)       : ORIGIN = 0x2000C000, LENGTH = 48K
  INVALID (rwx)   : ORIGIN = 0xFFFFFFFF, LENGTH = 0 /* Sanity check */
}

INCLUDE gcc_app_common.ld
