/* SPDX-License-Identifier: GPL-2.0-only
 * Own-source main-side gate, linked into the retired generic bridge's space.
 * QueryMemory examines exactly one fixed addon page, never a caller address.
 * Rejects an argument gap before reading or entering supplemental code.
 * Does not pair, send HCI, change permissions, allocate or retain pointers.
 * Each route tail-chains with original arguments and the original LR.
 */
.section .text
.balign 4
.global deferred_main_legacy_refuse, deferred_main_read_guard
.global deferred_main_relay_guard, deferred_main_capture_guard
deferred_main_legacy_refuse:
    cbnz x1, .Lnormal_hid
    mov w0, #-1
    ret
.Lnormal_hid:
    sub sp, sp, #0x30
    stp x29, x30, [sp, #0x10]
    stp x20, x19, [sp, #0x20]
    add x29, sp, #0x10
    b native_hid_body

.macro guard_entry name, selected, fallback
\name:
    adrp x16, \selected
    add x16, x16, :lo12:\selected
    adrp x17, \fallback
    add x17, x17, :lo12:\fallback
    b .Lguard
.endm
.global deferred_main_read_selected, deferred_main_relay_selected, deferred_main_capture_selected
deferred_main_read_guard:
    // Ordinary HID and the existing saved-peer commands never pay for a query.
    cbnz x1, previous_custom_mux
    cbz x2, previous_custom_mux
    ldp x16, x17, [x2]
    cmp x17, #16
    b.ne previous_custom_mux
    cbz x16, previous_custom_mux
    ldr w17, [x16]
    cmp w17, #11
    b.ne previous_custom_mux
    b deferred_main_read_selected
deferred_main_relay_guard:
    cmp w8, #0x6d
    b.ne previous_metadata_relay
    b deferred_main_relay_selected
deferred_main_capture_guard:
    cbz x0, .Lcapture_ignore
    ldrh w16, [x0]
    mov w17, #0xfd53
    cmp w16, w17
    b.ne .Lcapture_ignore
    b deferred_main_capture_selected
guard_entry deferred_main_read_selected, deferred_read_mux, previous_custom_mux
guard_entry deferred_main_relay_selected, deferred_metadata_relay, previous_metadata_relay
guard_entry deferred_main_capture_selected, capture_envelope, .Lcapture_ignore
.Lcapture_ignore:
    ret
.Lguard:
    // Preserve x0-x15 and LR across the syscall. x16/x17 are ABI scratch.
    // MemoryInfo[40] is at sp+0x90. Frame remains 16-byte aligned.
    sub sp, sp, #0xc0
    stp x0, x1, [sp]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    str x30, [sp, #0xb8]
    add x0, sp, #0x90
    adrp x2, deferred_image_base
    add x2, x2, :lo12:deferred_image_base
    svc #6
    ldp x16, x17, [sp, #0x80]
    cbnz w0, .Lrefuse
    ldr w3, [sp, #0xa8]
    cmp w3, #5
    b.ne .Lrefuse
    // Recompute the fixed page rather than assume syscall scratch survives.
    adrp x2, deferred_image_base
    add x2, x2, :lo12:deferred_image_base
    ldp x3, x4, [sp, #0x90]
    cmp x3, x2
    b.ne .Lrefuse
    cmp x4, #0x1000
    b.ne .Lrefuse
    // Read only after RX, exact page base/length are established.
    ldp w3, w4, [x2]
    mov w5, #2
    movk w5, #0x1400, lsl #16
    cmp w3, w5
    b.ne .Lrefuse
    cmp w4, #0x1000
    b.ne .Lrefuse
    ldp w3, w4, [x2, #8]
    mov w5, #0x03c0
    movk w5, #0xd65f, lsl #16
    cmp w3, w5
    b.ne .Lrefuse
    mov w5, #0x2c9f
    movk w5, #0xd503, lsl #16
    cmp w4, w5
    b.eq .Lrestore
.Lrefuse:
    mov x16, x17
.Lrestore:
    ldp x0, x1, [sp]
    ldp x2, x3, [sp, #0x10]
    ldp x4, x5, [sp, #0x20]
    ldp x6, x7, [sp, #0x30]
    ldp x8, x9, [sp, #0x40]
    ldp x10, x11, [sp, #0x50]
    ldp x12, x13, [sp, #0x60]
    ldp x14, x15, [sp, #0x70]
    ldr x30, [sp, #0xb8]
    add sp, sp, #0xc0
    br x16
.section .note.GNU-stack, "", %progbits
