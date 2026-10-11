/* SPDX-License-Identifier: GPL-2.0-only
 * Inert supplemental module for packaging qualification only.
 * No hooks, imports, constructors, relocations, driver access or radio traffic.
 */
.section .module_start, "ax", %progbits
.global runtime_probe_start
runtime_probe_start:
    b runtime_probe_return
    .word runtime_probe_mod0 - runtime_probe_start
runtime_probe_return:
    ret
    nop

.section .module_ro, "a", %progbits
.balign 8
.global runtime_probe_mod0
runtime_probe_mod0:
    .ascii "MOD0"
    .word runtime_probe_dynamic - runtime_probe_mod0
    .word runtime_probe_data_end - runtime_probe_mod0
    .word runtime_probe_data_end - runtime_probe_mod0
    .word 0
    .word 0
    .word runtime_probe_object - runtime_probe_mod0
    .word 0, 0, 0, 0, 0, 0
.balign 16
runtime_probe_dynamic:
    /* Linked at zero, so resolved symbol values are module-relative offsets. */
    .quad 4, runtime_probe_hash
    .quad 5, runtime_probe_strings
    .quad 6, runtime_probe_symbols
    .quad 10, 1
    .quad 11, 24
    .quad 0, 0
runtime_probe_hash:
    .word 1, 1, 0, 0
.balign 8
runtime_probe_symbols:
    .zero 24
runtime_probe_strings:
    .byte 0

.section .module_data, "aw", %progbits
.balign 16
.global runtime_probe_object
runtime_probe_object:
    /* A dedicated writable page, not shared with an endpoint slot. */
    .zero 4096
.global runtime_probe_data_end
runtime_probe_data_end:

.section .note.GNU-stack, "", %progbits
