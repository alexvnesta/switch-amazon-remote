/* SPDX-License-Identifier: GPL-2.0-only
 * Supplemental-image adapters. External bindings must be exact-build reviewed.
 * The capture callback does NOT enter this file or publish queue traffic.
 * The only new request copies an existing metadata record, never calls a radio
 * API, and is sent by the companion only after all interfaces/session are ready.
 * Native mutexes are NOT bounded by the synthetic test and are not retried.
 */
.section .text
.balign 4
.global deferred_read_mux, deferred_legacy_refuse, deferred_metadata_relay

// Original native method ABI: context x0, Bluetooth address x1, input descriptor
// x2. No output buffer exists. Only a fixed 16-byte command 11 is added here.
deferred_read_mux:
    cbnz x1, request_passthrough
    cbz x2, request_passthrough
    ldp x9, x8, [x2]
    cmp x8, #16
    b.ne request_passthrough
    cbz x9, request_passthrough
    ldr w10, [x9]
    cmp w10, #11
    b.ne request_passthrough
    ldr w10, [x9, #4]
    mov w11, #0x4341
    movk w11, #0x3152, lsl #16
    cmp w10, w11
    b.ne request_invalid
    ldr x10, [x9, #8]
    cbz x10, request_invalid
    sub sp, sp, #0x60
    stp x29, x30, [sp, #0x50]
    add x29, sp, #0x50
    mov w8, #0x4341
    movk w8, #0x314c, lsl #16
    mov w11, #1
    stp w8, w11, [sp]
    str x10, [sp, #8]
    add x0, sp, #16
    mov w1, #64
    bl read_record
    cbz w0, request_failed
    mov x0, sp
    mov w1, #80
    mov w2, #2
    mov w3, #0x6d
    bl native_queue
    // Forward queue admission/failure; neither means controller capability.
request_return:
    ldp x29, x30, [sp, #0x50]
    add sp, sp, #0x60
    ret
request_failed:
    mov w0, #-1
    b request_return
request_invalid:
    mov w0, #-1
    ret
request_passthrough:
    // The supplemental image may be farther than a conditional branch's 1 MiB
    // range. A local conditional target plus B keeps the tail path in range.
    b previous_custom_mux

// Retire the known-incompatible generic bridge. Previously reviewed saved-peer
// specialized commands still take their original path. Normal HID keeps the
// actual native frame and body. No generic HCI/config command is re-enabled.
deferred_legacy_refuse:
    cbnz x1, normal_hid
    mov w0, #-1
    ret
normal_hid:
    sub sp, sp, #0x30
    stp x29, x30, [sp, #0x10]
    stp x20, x19, [sp, #0x20]
    add x29, sp, #0x10
    b native_hid_body

// The ordinary consumer invokes this after its mailbox-availability checks.
// Every unselected event continues through the previous relay unchanged.
deferred_metadata_relay:
    cmp w8, #0x6d
    b.ne relay_passthrough
    ldr x9, [x25, #0x10]
    cmp x9, #80
    b.ne relay_drop
    ldr w9, [x28]
    mov w10, #0x4341
    movk w10, #0x314c, lsl #16
    cmp w9, w10
    b.ne relay_drop
    ldr w9, [x28, #4]
    cmp w9, #1
    b.ne relay_drop
    ldr x9, [x28, #8]
    cbz x9, relay_drop
    adrp x25, core_mailbox
    add x25, x25, :lo12:core_mailbox
    add x0, x25, #0x30
    bl native_lock
    mov w8, #80
    str x8, [x25, #0x38]
    mov w8, #0x6d
    str w8, [x25, #0x440]
    ldp x8, x9, [x28]
    stp x8, x9, [x25, #0x40]
    ldp x8, x9, [x28, #16]
    stp x8, x9, [x25, #0x50]
    ldp x8, x9, [x28, #32]
    stp x8, x9, [x25, #0x60]
    ldp x8, x9, [x28, #48]
    stp x8, x9, [x25, #0x70]
    ldp x8, x9, [x28, #64]
    stp x8, x9, [x25, #0x80]
    b relay_publish
relay_passthrough:
    b previous_metadata_relay
relay_drop:
    b queue_pop
