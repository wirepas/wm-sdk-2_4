/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Generic Bootloader Updater Tool entry points for 16 and
 * 32 kB bootloaders, as well as the second stage loader
 */

    /* 16 kB keepout area for the bootloader updater,
     * not present in the second stage loader
     */

    .if BOOTLOADER_UPDATER

    .syntax unified
    .arch armv6-m

    .section .keepout, "ax"
    .thumb

    .thumb_func
keepout:
    /* Branch to actual entry point */
    push    {r0, r1}
    ldr     r0, 1f
    str     r0, [sp, #4]
    pop     {r0, pc}
    .align  2
1:
    .word   entrypoint
    .word   0

bl_info_header_keepout:
    /* Filled in by the 16 kB bootloader */
    .long   0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff

    .global bl_updater_info_header_keepout
bl_updater_info_header_keepout:
    /* Filled in by the bootloader updater */
    .long   0xffffffff, 0xffffffff

__for_future_use_keepout__:
    .long   0xffffffff

    .endif  /* BOOTLOADER_UPDATER */


    /* Real entry point for the bootloader updater and second stage loader */

    .section .entrypoint, "ax"
    .thumb

    .thumb_func
entrypoint:
    /* Branch directly to _bl_updater_start(), in bootloader.a */
    push    {r0, r1}
    ldr     r0, 1f
    str     r0, [sp, #4]
    pop     {r0, pc}
    .align  2
1:
    .if BOOTLOADER_UPDATER
    .word   _bl_updater_start
    .word   0
    .endif  /* BOOTLOADER_UPDATER */

    .if SECOND_STAGE_LOADER
    .word   _bl_updater_2nd_start
    .word   0
    .endif  /* SECOND_STAGE_LOADER */

bl_info_header:
    /* Filled in by the 32 kB bootloader */
    .long   0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff

    .global bl_updater_info_header
bl_updater_info_header:
    /* Filled in by the bootloader updater */
    .long   0xffffffff, 0xffffffff

__for_future_use__:
    .long   0xffffffff
