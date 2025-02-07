/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/* Linker script for Wirepas Generic Bootloader Updater Tool, common parts
 */

SECTIONS
{
    .entrypoint :
    {
        /* Generic Bootloader Updater Tool entry point */
        __entrypoint_start__ = .;
        KEEP(*(.entrypoint))
    } >ENTRYPOINT

    .text :
    {
        __rom_start__ = .;
        *(.romtext)
        *(.text.*)
        *(.text)
        *(.glue_7)
        *(.glue_7t)
        *(.vfp11_veneer)
        *(.v4_bx)
        *(.rodata.*)
        *(.rodata)
        *(.rodata1)
        *(.crc_lut)
        . = ALIGN(8);
    } >BL_UPDATER

    __etext = .;
    __data_src_start__ = .;

    .rtt (NOLOAD):
    {
        /* Force placing _SEGGER_RTT control block here, if RTT tracing is on */
        *(.rtt_cb_section)
        . = ALIGN(8);
    } >RAM

    .data :
    {
        __data_start__ = .;
        *(.ramtext)
        *(.data.*)
        *(.data)
        *(.data1)
        . = ALIGN(8);
        __data_end__ = .;
    } >RAM AT >BL_UPDATER

    __rom_end__ = __data_src_start__ + SIZEOF(.data);

    .bss :
    {
        __bss_start__ = .;
        *(.bss.*)
        *(.bss)
        *(COMMON)
        . = ALIGN(8);
        __erase_sector_buffer_start__ = .;
        . += __erase_sector_size__;
        __erase_sector_buffer_end__ = .;
        __bss_end__ = .;
    } >RAM

    /* .stack_dummy section doesn't contain any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later */
    .stack_dummy (COPY):
    {
        *(.stack*)
    } > RAM

    /* Set stack top to end of RAM, and stack limit move down by
     * size of stack_dummy section */
    __stack_top__ = ORIGIN(RAM) + LENGTH(RAM);

    .invalid :
    {
        *(.init)
        *(.fini)
        *(.preinit_array)
        *(.init_array)
        *(.fini_array)
        *(.ctors)
        *(.dtors)
        *(.jcr)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.heap*)
        *(.tbss)
        *(.tdata)
        *(.tdata1)
        *(.got)
        *(.got.plt)
        *(.igot.plt)
        *(.iplt)
        *(.rel.dyn)
        *(.rel.iplt)
        *\libgcc.a:*df*.o
        *\libgcc.a:*sf*.o
    } >INVALID  /* Linking fails if any of these sections have contents */

    /* C++ exception unwinding information is silently discarded */
    /DISCARD/ :
    {
        *(.ARM.extab)
        *(.ARM.extab.*)
        *(.ARM.exidx)
        *(.ARM.exidx.*)
    }
}
