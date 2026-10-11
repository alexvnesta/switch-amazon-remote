/* SPDX-License-Identifier: GPL-2.0-only
 * Exact-build, target-only security metadata. No key/payload extraction.
 * Link addresses and the six-byte target are supplied by the private review.
 * All delivery uses the existing bounded driver queue and its sole consumer.
 */
.section .text
.balign 4
.global callback_tap, relay_dispatch, probe_dispatch, custom_mux, target_address
// The current MC custom hook does not replay this stored image's prologue.
// Isolate ONLY our exact command 6. Other inputs take the existing MC route.
custom_mux:
    cbnz x1, legacy_custom
    cbz x2, legacy_custom
    ldp x9, x8, [x2]
    cmp x8, #12
    b.ne legacy_custom
    cbz x9, legacy_custom
    ldr w10, [x9]
    cmp w10, #6
    b.ne legacy_custom
    sub sp, sp, #0x30
    stp x29, x30, [sp, #0x10]
    stp x20, x19, [sp, #0x20]
    add x29, sp, #0x10
    mov x19, x9
    mov w9, #6
    add x19, x19, #4
    b probe_dispatch
callback_tap:
    sub sp, sp, #0x240             // displaced original instruction
    cmp w0, #3
    b.eq 1f
    cmp w0, #12
    b.ne callback_resume
1:  cbz x1, callback_resume
    adr x8, target_address
    ldr w9, [x1]
    ldr w10, [x8]
    cmp w9, w10
    b.ne callback_resume
    ldrh w9, [x1, #4]
    ldrh w10, [x8, #4]
    cmp w9, w10
    b.ne callback_resume
    sub sp, sp, #0x20
    stp x0, x1, [sp, #0x10]
    mov w8, #0x5241
    movk w8, #0x314d, lsl #16
    str w8, [sp]
    ldr w8, [x1]
    str w8, [sp, #4]
    ldrh w8, [x1, #4]
    strh w8, [sp, #8]
    strb w0, [sp, #10]
    cmp w0, #12
    b.ne 2f
    ldrb w8, [x1, #6]              // key TYPE only, never key union
    b 3f
2:  ldrb w8, [x1, #0x111]          // native authentication success byte
3:  strb w8, [sp, #11]
    str wzr, [sp, #12]
    mov x0, sp
    mov w1, #16
    mov w2, #2
    mov w3, #0x6a
    bl native_queue
    ldp x0, x1, [sp, #0x10]
    add sp, sp, #0x20
    b callback_resume

// Runs in the existing queue consumer, AFTER its empty-mailbox checks.
// Other event types retain the original compare and dispatch.
relay_dispatch:
    cmp w8, #0x6a
    b.eq 4f
    cmp w8, #0x2b                 // displaced original instruction
    b relay_resume
4:  ldr x9, [x25, #0x10]          // queued payload length
    cmp x9, #16
    b.ne queue_pop
    ldr w9, [x28]
    mov w10, #0x5241
    movk w10, #0x314d, lsl #16
    cmp w9, w10
    b.ne queue_pop
    adrp x25, core_mailbox
    add x25, x25, :lo12:core_mailbox
    add x0, x25, #0x30
    bl native_lock
    mov w8, #16
    str x8, [x25, #0x38]
    mov w8, #0x6a
    str w8, [x25, #0x440]
    ldp x8, x9, [x28]
    stp x8, x9, [x25, #0x40]
    b relay_publish              // original unlock, signal, queue pop

// New command 6 is only a nonce round-trip. It never calls the radio stack.
// Existing commands 0..5 and their behavior are unchanged.
probe_dispatch:
    cmp w9, #6
    b.ne probe_return
    cmp x8, #12
    b.ne probe_invalid
    ldur w10, [x19, #-4]
    cmp w10, #6
    b.ne probe_invalid
    ldr w10, [x19]
    mov w11, #0x5241
    movk w11, #0x314d, lsl #16
    cmp w10, w11
    b.ne probe_invalid
    ldr w10, [x19, #4]
    cbz w10, probe_invalid
    sub sp, sp, #0x10
    str w11, [sp]
    str w10, [sp, #12]
    adr x11, target_address
    ldr w10, [x11]
    str w10, [sp, #4]
    ldrh w10, [x11, #4]
    strh w10, [sp, #8]
    mov w10, #0x100               // kind 0, detail 1, no security claim
    strh w10, [sp, #10]
    mov x0, sp
    mov w1, #16
    mov w2, #2
    mov w3, #0x6a
    bl native_queue
    add sp, sp, #0x10
    b custom_return
probe_invalid:
    mov w0, #-1
probe_return:
    b custom_return
.balign 4
target_address:
    .byte 0,0,0,0,0,0              // private build substitutes exact target
    .byte 0,0
