/* SPDX-License-Identifier: GPL-2.0-only */
OUTPUT_ARCH(aarch64)
ENTRY(deferred_main_legacy_refuse)
SECTIONS {
    . = deferred_main_guard_base;
    .text : { *(.text .text.*) }
    .forbidden : {
        *(.rodata* .data* .bss* COMMON .got* .plt* .init* .fini* .preinit*)
        *(.dynamic .dynsym .dynstr .rel* .tdata* .tbss* .eh_frame* .gcc_except_table*)
    }
    ASSERT(deferred_main_legacy_refuse==deferred_main_guard_base, "Legacy entry moved")
    ASSERT(SIZEOF(.text)<=deferred_main_guard_capacity, "Guard exceeds retired bridge")
    ASSERT(SIZEOF(.forbidden)==0, "Unexpected storage or runtime dependency")
    /DISCARD/ : { *(.comment .note.GNU-stack) }
}
