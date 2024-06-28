/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Generic Bootloader Updater Tool entry point */

    .syntax unified
    .arch armv6-m

    .section .entrypoint, "ax"
    .thumb

entrypoint:
    /* Branch directly to _bl_updater_start(), in bootloader.a */
    push    {r0, r1}
    ldr     r0, 1f
    str     r0, [sp, #4]
    pop     {r0, pc}
    .align  2
1:
    .word   _bl_updater_start
    .word   0

bl_info_header:
    /* Filled in by the bootloader */
    .long   0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff

__for_future_use__:
    .long   0xffffffff, 0xffffffff, 0xffffffff
