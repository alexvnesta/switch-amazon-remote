/* SPDX-License-Identifier: GPL-2.0-only
 * Supplemental observer metadata. Its legacy entry is deliberately inert.
 * The native runtime object owns a complete page; endpoint storage follows it.
 * No imports, exports, relocations, initializers or finalizers are declared.
 */
.section .module_start, "ax", %progbits
.global deferred_runtime_start
deferred_runtime_start:
    b deferred_runtime_return
    .word deferred_runtime_mod0 - deferred_runtime_start
deferred_runtime_return:
    ret
    // Unexecuted ABI marker: distinguishes D2 from the inert L2 addon.
    hint #0x64

.section .module_ro, "a", %progbits
.balign 8
.global deferred_runtime_mod0
deferred_runtime_mod0:
    .ascii "MOD0"
    .word deferred_runtime_dynamic - deferred_runtime_mod0
    .word deferred_runtime_data_end - deferred_runtime_mod0
    .word deferred_runtime_data_end - deferred_runtime_mod0
    .word 0, 0
    .word deferred_runtime_object - deferred_runtime_mod0
    .word 0, 0, 0, 0, 0, 0
.balign 16
deferred_runtime_dynamic:
    /* Module-relative values, independently asserted by the linker/checker. */
    .quad 4, 0x10a0
    .quad 5, 0x10c8
    .quad 6, 0x10b0
    .quad 10, 1
    .quad 11, 24
    .quad 0, 0
.global deferred_runtime_hash, deferred_runtime_symbols, deferred_runtime_strings
deferred_runtime_hash:
    .word 1, 1, 0, 0
.balign 8
deferred_runtime_symbols:
    .zero 24
deferred_runtime_strings:
    .byte 0

.section .module_data, "aw", %progbits
.balign 16
.global deferred_runtime_object
deferred_runtime_object:
    .zero 4096
.section .note.GNU-stack, "", %progbits
