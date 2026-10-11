/* SPDX-License-Identifier: GPL-2.0-only */
OUTPUT_ARCH(aarch64)
ENTRY(runtime_probe_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0;
    .text : { KEEP(*(.module_start)) } :text
    . = 0x1000;
    .rodata : { KEEP(*(.module_ro)) } :rodata
    . = 0x2000;
    .data : { KEEP(*(.module_data)) } :data
    .forbidden : {
        *(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *(.init* .fini* .preinit* .dynamic .dynsym .dynstr .rel* .got* .plt* .tdata* .tbss*)
        *(.eh_frame* .gcc_except_table*)
    } :data
    ASSERT(SIZEOF(.text)==16, "Inert entry changed")
    ASSERT(SIZEOF(.rodata)<=0x1000, "Runtime metadata exceeds its page")
    ASSERT(SIZEOF(.data)==0x1000, "Dedicated runtime-object page changed")
    ASSERT(SIZEOF(.forbidden)==0, "Unexpected runtime behavior or dependency")
    /DISCARD/ : { *(.comment .note.GNU-stack) }
}
