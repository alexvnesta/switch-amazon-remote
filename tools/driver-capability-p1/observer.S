/* SPDX-License-Identifier: GPL-2.0-only
 * Retained direct-queue comparison candidate only. No command sender.
 * Startup delivery is NOT qualified. Prefer the deferred capture design;
 * this file is not the next deployment candidate.
 * Link bindings and firmware verification belong to the private exact-build
 * review. Do not install without the matching event consumer and full review.
 */
.section .text
.balign 4
.global legacy_refuse, capability_callback, metadata_relay

// Replaces the incompatible legacy bridge. Real HID writes retain the exact
// original frame and native body. Zero-address generic commands fail closed.
// Existing specialized saved-peer commands bypass this legacy entry entirely.
legacy_refuse:
    cbnz x1, normal_hid
    mov w0, #-1
    ret
normal_hid:
    sub sp, sp, #0x30
    stp x29, x30, [sp, #0x10]
    stp x20, x19, [sp, #0x20]
    add x29, sp, #0x10
    b native_hid_body

// Borrowed vendor completion envelope: u16 opcode, u16 length, padding,
// pointer at +8. The original callback is a no-op. Copy only the selected
// status or nine-byte capability prefix while the caller still owns it.
capability_callback:
    cbz x0, callback_return
    ldrh w8, [x0]
    mov w9, #0xfd53
    cmp w8, w9
    b.ne callback_return
    ldrh w9, [x0, #2]
    ldr x10, [x0, #8]
    sub sp, sp, #0x30
    stp x29, x30, [sp, #0x20]
    add x29, sp, #0x20
    stp xzr, xzr, [sp]
    stp xzr, xzr, [sp, #0x10]
    mov w8, #0x4341
    movk w8, #0x3150, lsl #16
    mov w11, #1
    stp w8, w11, [sp]
    str w9, [sp, #8]
    cbz w9, metadata_publish
    cmp w9, #252
    b.hi metadata_publish
    cbz x10, metadata_publish
    ldrb w8, [x10]
    cbnz w8, error_status
    cmp w9, #9
    b.lo metadata_publish
    cmp w9, #10
    b.eq metadata_publish
    ldr x8, [x10]
    ldrb w9, [x10, #8]
    str x8, [sp, #16]
    strb w9, [sp, #24]
    mov w8, #9
    str w8, [sp, #12]
    b metadata_publish
error_status:
    strb w8, [sp, #16]
    mov w8, #1
    str w8, [sp, #12]
metadata_publish:
    mov x0, sp
    mov w1, #32
    mov w2, #2
    mov w3, #0x6c
    bl native_queue
    // Queue failure is not controller rejection. No retry or radio operation.
    ldp x29, x30, [sp, #0x20]
    add sp, sp, #0x30
callback_return:
    ret

// Called from the existing queue consumer at its event discriminator. Other
// events replay the displaced comparison and continue through the old route.
metadata_relay:
    cmp w8, #0x6c
    b.eq selected_relay
    cmp w8, #0x6b
    b previous_relay_continue
selected_relay:
    ldr x9, [x25, #0x10]
    cmp x9, #32
    b.ne queue_pop
    ldr w9, [x28]
    mov w10, #0x4341
    movk w10, #0x3150, lsl #16
    cmp w9, w10
    b.ne queue_pop
    ldr w9, [x28, #4]
    cmp w9, #1
    b.ne queue_pop
    adrp x25, core_mailbox
    add x25, x25, :lo12:core_mailbox
    add x0, x25, #0x30
    bl native_lock
    mov w8, #32
    str x8, [x25, #0x38]
    mov w8, #0x6c
    str w8, [x25, #0x440]
    ldp x8, x9, [x28]
    stp x8, x9, [x25, #0x40]
    ldp x8, x9, [x28, #16]
    stp x8, x9, [x25, #0x50]
    b relay_publish
