/* SPDX-License-Identifier: GPL-2.0-only */
OUTPUT_ARCH(aarch64)
ENTRY(deferred_runtime_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = deferred_image_base;
    .text : { KEEP(*(.module_start)) *(.text .text.*) } :text
    . = deferred_image_base + 0x1000;
    .rodata : { KEEP(*(.module_ro)) *(.rodata .rodata.*) } :rodata
    . = deferred_image_base + 0x2000;
    .data : { KEEP(*(.module_data)) *(.data .data.*) } :data
    deferred_runtime_data_end = .;
    .forbidden : {
        *(.bss .bss.* COMMON .got* .plt* .init* .fini* .preinit*)
        *(.dynamic .dynsym .dynstr .rela.* .rel.* .tdata* .tbss*)
        *(.eh_frame* .gcc_except_table*)
    } :data
    ASSERT(SIZEOF(.text)<=0x1000, "Observer text exceeds its page")
    ASSERT(SIZEOF(.rodata)<=0x1000, "Observer metadata exceeds its page")
    ASSERT(deferred_runtime_hash-deferred_image_base==0x10a0, "Hash binding changed")
    ASSERT(deferred_runtime_symbols-deferred_image_base==0x10b0, "Symbols binding changed")
    ASSERT(deferred_runtime_strings-deferred_image_base==0x10c8, "Strings binding changed")
    ASSERT(deferred_runtime_object-deferred_image_base==0x2000, "Runtime object moved")
    ASSERT(deferred_runtime_slot-deferred_image_base==0x3000, "Endpoint slot overlaps runtime object")
    ASSERT(SIZEOF(.data)==0x1024, "Owned data layout changed")
    ASSERT(SIZEOF(.forbidden)==0, "Unexpected runtime dependency or mutable storage")
    /DISCARD/ : { *(.comment .note.GNU-stack) }
}
