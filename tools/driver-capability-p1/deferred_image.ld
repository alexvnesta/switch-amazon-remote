/* SPDX-License-Identifier: GPL-2.0-only
 * Own-source supplemental image. An exact-build integration supplies the
 * image base and reviewed external bindings. No constructors or relocations.
 */
OUTPUT_ARCH(aarch64)
ENTRY(capture_envelope)
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = deferred_image_base;
    .text : { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .forbidden : {
        *(.bss .bss.* COMMON .got .got.* .plt .plt.*)
        *(.init .fini .init_array* .fini_array* .preinit_array*)
        *(.dynamic .dynsym .dynstr .rela.* .rel.* .tdata* .tbss*)
        *(.eh_frame* .gcc_except_table*)
    } :data
    ASSERT(SIZEOF(.forbidden)==0, "Unexpected runtime, relocation or mutable storage")
    ASSERT(SIZEOF(.data)==36, "Owned slot changed")
    ASSERT(SIZEOF(.text)<=0x1000, "Text exceeds one page")
    ASSERT(SIZEOF(.rodata)==16, "Metadata changed")
    /DISCARD/ : { *(.comment .note.GNU-stack) }
}
