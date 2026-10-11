/* SPDX-License-Identifier: GPL-2.0-only
 * Exact-build, fixed-peer central bond bridge. No general memory/key getter.
 * PENC events go ONLY to the companion privileged driver consumer, which must
 * consume and erase them before any public forwarding/logging. Never use this
 * bridge without that matching consumer. Link bindings are privately reviewed.
 * Stock SMP, key storage and request-specific encryption callbacks are reused.
 */
.section .text
.balign 4
.global key_tap, relay_dispatch, custom_mux, dm_mux, target_address

custom_mux:
    cbnz x1, legacy_custom
    cbz x2, legacy_custom
    ldp x9, x8, [x2]
    cmp x8, #44
    b.ne legacy_custom
    cbz x9, legacy_custom
    ldr w10, [x9]
    sub w11, w10, #7
#ifdef ARMODULE_NATIVE_PEER_C1
    cmp w11, #3
#else
    cmp w11, #2
#endif
    b.hi legacy_custom
    sub sp, sp, #0x30
    stp x29, x30, [sp, #0x10]
    stp x20, x19, [sp, #0x20]
    add x29, sp, #0x10
    mov x19, x9
    mov w20, w10
    ldr w10, [x19, #4]
    mov w11, #0x5241
    movk w11, #0x3142, lsl #16
    cmp w10, w11
    b.ne command_invalid
#ifdef ARMODULE_NATIVE_HCI_C2
    cmp w20, #9
    b.eq command_invalid     // saved-only diagnostic: never enroll
#endif
    ldr w10, [x19, #8]
    cbz w10, command_invalid
    cmp w20, #8
    b.eq hello
    ldr w10, [x19, #40]
    cmp w10, #0xeff
    b.hi command_invalid
    cmp w20, #9
#ifdef ARMODULE_NATIVE_PEER_C1
    b.hs allocate_request
#else
    b.eq allocate_request
#endif
    ldrh w10, [x19, #38]
    mov w11, #0x1001           // audited Just Works security1, length16
    cmp w10, w11
    b.ne command_invalid
allocate_request:
    mov w0, #64
    bl native_alloc
    cbz x0, command_invalid
    mov w8, #0x13d            // first unused DM selector, handled below
    stp xzr, xzr, [x0]
    stp xzr, xzr, [x0, #16]
    stp xzr, xzr, [x0, #32]
    stp xzr, xzr, [x0, #48]
    strh w8, [x0]
#ifdef ARMODULE_NATIVE_PEER_C1
    sub w8, w20, #7
    cmp w8, #0
    csinc w8, w8, wzr, ne     // 7->1 restore, 9->2 enroll, 10->3 prepare
#else
    mov w8, #1
    cmp w20, #9
    cinc w8, w8, eq
#endif
    strb w8, [x0, #8]         // stable BTM action/reference first byte
    ldr w8, [x19, #8]
    str w8, [x0, #12]
    cmp w20, #9
#ifdef ARMODULE_NATIVE_PEER_C1
    b.hs queued_target
#else
    b.eq queued_target
#endif
    ldur x8, [x19, #12]
    ldur x9, [x19, #20]
    ldr x10, [x19, #28]
    ldr w11, [x19, #36]
    stp x8, x9, [x0, #16]
    str x10, [x0, #32]
    str w11, [x0, #40]
queued_target:
    ldrh w8, [x19, #40]
    strh w8, [x0, #44]
    adr x8, target_address
    ldr w9, [x8]
    ldrh w10, [x8, #4]
    str w9, [x0, #48]
    strh w10, [x0, #52]
    bl dm_queue
    mov w0, wzr
    b custom_return
command_invalid:
    mov w0, #-1
    b custom_return
hello:
    sub sp, sp, #64
    stp xzr, xzr, [sp]
    stp xzr, xzr, [sp, #16]
    stp xzr, xzr, [sp, #32]
    stp xzr, xzr, [sp, #48]
    mov w8, #0x100             // kind0, version1
    strh w8, [sp, #10]
    ldr w8, [x19, #8]
    str w8, [sp, #12]
    mov w8, #0xffff
    strh w8, [sp, #44]
    adr x0, target_address
    bl find_device
    cbz x0, hello_publish
    ldrh w8, [x0, #0x168]
    strh w8, [sp, #44]
hello_publish:
    mov x0, sp
    bl publish
    add sp, sp, #64
    mov w0, wzr
    b custom_return

#ifdef ARMODULE_NATIVE_HCI_C2
.global hci_status_tap
// Passive call-site tap from the RECEIVED HCI Command Status event parser.
// The common dispatcher also handles synthetic host timeouts; do NOT tap its
// entry or label those locally synthesized results as controller responses.
// x2 points to the ORIGINAL command's parameter-length byte, not the event.
// Only an exact fixed-public-peer, policy0, 25-byte LE create command qualifies.
// No raw command bytes are published. All original arguments remain intact.
hci_status_tap:
    mov w8, #0x200d
    cmp w0, w8
    b.ne hci_status_resume_original
    cbz x2, hci_status_resume_original
    ldrb w8, [x2]
    cmp w8, #25
    b.ne hci_status_resume_original
    ldurh w8, [x2, #5]       // filter policy and peer address type both0
    cbnz w8, hci_status_resume_original
    adr x9, target_wire_address
    ldur w8, [x2, #7]
    ldr w10, [x9]
    cmp w8, w10
    b.ne hci_status_resume_original
    ldurh w8, [x2, #11]
    ldrh w10, [x9, #4]
    cmp w8, w10
    b.ne hci_status_resume_original
    sub sp, sp, #112
    stp x0, x1, [sp, #64]
    stp x2, x3, [sp, #80]
    stp x4, x30, [sp, #96]
    stp xzr, xzr, [sp]
    stp xzr, xzr, [sp, #16]
    stp xzr, xzr, [sp, #32]
    stp xzr, xzr, [sp, #48]
    mov w8, #6
    strb w8, [sp, #10]
    strb w1, [sp, #11]       // raw controller command status, not link status
    ldrb w8, [x2, #13]
    strb w8, [sp, #16]       // own address TYPE only, never address/key material
    str w0, [sp, #20]
    mov x0, sp
    bl publish
    ldp x0, x1, [sp, #64]
    ldp x2, x3, [sp, #80]
    ldp x4, x30, [sp, #96]
    add sp, sp, #112
hci_status_resume_original:
    b hci_status_original   // original called routine, all arguments unchanged
.balign 4
target_wire_address:
    .quad 0
#else
key_tap:
    sub sp, sp, #0x40          // displaced stock key-store prologue
    cmp w1, #1                // PENC only, no IRK/signing/local roots
    b.ne store_resume
    cbz x2, store_resume
    cbz w3, store_resume      // restoration must not be recaptured
    adr x8, target_address
    ldr w9, [x0]
    ldr w10, [x8]
    cmp w9, w10
    b.ne store_resume
    ldrh w9, [x0, #4]
    ldrh w10, [x8, #4]
    cmp w9, w10
    b.ne store_resume
    sub sp, sp, #112
    stp x0, x1, [sp, #64]
    stp x2, x3, [sp, #80]
    stp x29, x30, [sp, #96]
    bl find_device
    cbz x0, capture_return
    ldr x9, [x0, #8]
    adr x10, encryption_complete
    cmp x9, x10
    b.ne capture_return
    ldr x9, [x0, #16]
    cbz x9, capture_return
    ldrb w8, [x9]
    cmp w8, #2                // only OUR explicit enrollment, not reconnect
    b.ne capture_return
    stp xzr, xzr, [sp]
    stp xzr, xzr, [sp, #16]
    stp xzr, xzr, [sp, #32]
    stp xzr, xzr, [sp, #48]
    ldr w8, [x9, #4]          // original private enrollment request nonce
    str w8, [sp, #12]
    mov w8, #1
    strb w8, [sp, #10]
    ldr x2, [sp, #80]
    ldp x8, x9, [x2]
    ldr x10, [x2, #16]
    ldr w11, [x2, #24]
    stp x8, x9, [sp, #16]
    str x10, [sp, #32]
    str w11, [sp, #40]
    mov x0, sp
    bl publish
    // Clear secret-bearing scratch before resuming the original callback.
    stp xzr, xzr, [sp, #16]
    stp xzr, xzr, [sp, #32]
capture_return:
    ldp x0, x1, [sp, #64]
    ldp x2, x3, [sp, #80]
    ldp x29, x30, [sp, #96]
    add sp, sp, #112
    b store_resume
#endif

// The existing DM queue serializes restore, confirmation and encryption start.
// Normal messages replay the displaced byte load and original selector check.
dm_mux:
    ldrh w8, [x0]
    cmp w8, #0x13d
    b.eq restore_owned
    ldrb w8, [x0]
    b dm_resume
restore_owned:
    stp x29, x30, [sp, #-32]!
    stp x20, x19, [sp, #16]
    mov x19, x0
#ifdef ARMODULE_NATIVE_PEER_C1
    ldrb w8, [x19, #8]
    cmp w8, #3
    b.eq prepare_peer
#endif
    add x0, x19, #48
    mov w1, #2
    bl find_acl
    cbz x0, restore_fail
    ldrb w8, [x0, #0x12b]
    cbnz w8, restore_fail      // central only
    add x0, x19, #48
    bl find_device
    cbz x0, restore_fail
    mov x20, x0
    ldrb w8, [x20, #0x1c4]
    cmp w8, #2
    b.ne restore_fail
    ldrb w8, [x20, #0x16c]
    cbnz w8, restore_fail      // this tested remote's public identity only
    ldrb w8, [x20, #0x15a]
    cbnz w8, restore_fail
    ldr x8, [x20, #8]
    cbnz x8, restore_fail      // never replace another native callback
    ldrh w8, [x20, #0x168]
    cmp w8, #0xeff
    b.hi restore_fail
    ldrh w9, [x19, #44]
    cmp w8, w9
    b.ne restore_fail
    ldrb w8, [x19, #8]
    cmp w8, #2
    b.eq start_security       // explicit enrollment, no restore/fallback
    add x0, x19, #48
    mov w1, #1
    add x2, x19, #16
    mov w3, wzr
    bl store_key              // stock PENC store, no new-pair callback
    ldp x8, x9, [x19, #16]
    add x12, x20, #0x18c
    ldp x10, x11, [x12]
    eor x8, x8, x10
    eor x9, x9, x11
    orr x8, x8, x9
    cbnz x8, restore_fail
    ldr x8, [x19, #32]
    ldr x9, [x12, #32]
    cmp x8, x9
    b.ne restore_fail
    ldrh w8, [x19, #40]
    ldrh w9, [x20, #0x1b4]
    cmp w8, w9
    b.ne restore_fail
    ldrh w8, [x19, #42]
    ldrh w9, [x20, #0x1b8]
    cmp w8, w9
    b.ne restore_fail
    stp xzr, xzr, [x19, #16]
    str xzr, [x19, #32]
    str wzr, [x19, #40]
start_security:
    add x0, x19, #48
    mov w1, #2
    adr x2, encryption_complete
    add x3, x19, #8
    bl set_encryption         // existing request-specific native callback
    b restore_return
restore_fail:
    add x0, x19, #48
    mov w1, #2
    add x2, x19, #8
    mov w3, #0xf0             // bridge admission/readback failed
    bl encryption_complete
restore_return:
    // Retain one64-byte request until driver shutdown, including on timeout.
    // The owner admits only one restore per boot. Never free a pending ref.
    ldp x20, x19, [sp, #16]
    ldp x29, x30, [sp], #32
    mov w0, wzr               // generic dispatcher must not free this request
    ret

#ifdef ARMODULE_NATIVE_PEER_C1
// Serialized, fixed-peer metadata only. The privileged consumer admits this
// once per boot only for its validated SAVED record and current owner/window.
// No keys, pairing, link request, filter change or arbitrary peer import.
prepare_peer:
    add x0, x19, #48
    bl find_device
    cmp x0, #0
    cset w8, ne
    strb w8, [x19, #46]       // native record existed before preparation
    cbnz x0, peer_check
    add x0, x19, #48
    mov x1, xzr
    mov w2, #2
    mov w3, wzr
    bl add_ble_device         // stock allocation; full cache fails, no eviction
    cbz w0, peer_fail
    add x0, x19, #48
    bl find_device
    cbz x0, peer_fail
peer_check:
    ldrb w8, [x0, #0x1c4]
    cmp w8, #2
    b.ne peer_fail
    ldrb w8, [x0, #0x16c]
    cbnz w8, peer_fail
    ldrb w8, [x0, #0x15a]
    cbnz w8, peer_fail
    ldr x8, [x0, #8]
    cbnz x8, peer_fail
    mov w9, #0xffff
    ldrh w8, [x0, #0x168]
    cmp w8, w9
    b.ne peer_fail
    ldrh w8, [x0, #0x28]
    cmp w8, w9
    b.ne peer_fail
    add x0, x19, #48
    add x1, x19, #44
    add x2, x19, #45
    bl read_dev_info          // same native resolution used before connection
    ldrh w8, [x19, #44]
    cmp w8, #2                // BLE2 / public0; inquiry cannot silently override
    b.ne peer_fail
    mov w8, #5
    b peer_result
peer_fail:
    mov w8, #0xf005
peer_result:
    strh w8, [x19, #10]       // kind5, status0 or F0; public scalars only
#ifdef ARMODULE_NATIVE_HCI_C2
    mov w8, #2
    strb w8, [x19, #47]       // matched passive status tap revision, not success
#endif
    mov x0, x19
    bl publish
    b restore_return
#endif

encryption_complete:
    stp x29, x30, [sp, #-32]!
    stp x20, x19, [sp, #16]
    sub x19, x2, #8
    mov w20, w3
    // Correlation additionally checked by the consumer's connection epoch.
    cmp w1, #2
    b.ne completion_bad
    cbz x0, completion_bad
    ldr w8, [x0]
    ldr w9, [x19, #48]
    cmp w8, w9
    b.ne completion_bad
    ldrh w8, [x0, #4]
    ldrh w9, [x19, #52]
    cmp w8, w9
    b.ne completion_bad
    cbnz w20, completion_send
    add x0, x19, #48
    bl find_device
    cbz x0, completion_bad
    ldrh w8, [x0, #0x168]
    ldrh w9, [x19, #44]
    cmp w8, w9
    b.ne completion_bad
    ldrh w8, [x0, #0x46]
    tbz w8, #10, completion_bad
    ldrb w8, [x0, #0x16a]
    cmp w8, #16
    b.ne completion_bad
    mov w8, #0x1001
    strh w8, [x19, #46]       // encrypted1, effective key length16
    b completion_send
completion_bad:
    mov w20, #0xf1
completion_send:
    // Every completion, including failure, erases the queued PENC copy.
    stp xzr, xzr, [x19, #16]
    str xzr, [x19, #32]
    str wzr, [x19, #40]
    mov w8, #4
    strb w8, [x19, #10]
    strb w20, [x19, #11]
    mov x0, x19
    bl publish
    ldp x20, x19, [sp, #16]
    ldp x29, x30, [sp], #32
    ret

// Only the internal native event queue. No file, logger or public key getter.
publish:
    mov w8, #0x5241
    movk w8, #0x3142, lsl #16
    str w8, [x0]
    adr x8, target_address
    ldr w9, [x8]
    ldrh w10, [x8, #4]
    str w9, [x0, #4]
    strh w10, [x0, #8]
    mov w1, #64
    mov w2, #2
    mov w3, #0x6b
    b native_queue

relay_dispatch:
    cmp w8, #0x6b
    b.eq relay_private
    cmp w8, #0x2b
    b relay_resume
relay_private:
    ldr x9, [x25, #0x10]
    cmp x9, #64
    b.ne queue_pop
    ldr w9, [x28]
    mov w10, #0x5241
    movk w10, #0x3142, lsl #16
    cmp w9, w10
    b.ne queue_pop
    adrp x25, core_mailbox
    add x25, x25, :lo12:core_mailbox
    add x0, x25, #0x30
    bl native_lock
    mov w8, #64
    str x8, [x25, #0x38]
    mov w8, #0x6b
    str w8, [x25, #0x440]
    ldp x8, x9, [x28]
    stp x8, x9, [x25, #0x40]
    ldp x8, x9, [x28, #16]
    stp x8, x9, [x25, #0x50]
    ldp x8, x9, [x28, #32]
    stp x8, x9, [x25, #0x60]
    ldp x8, x9, [x28, #48]
    stp x8, x9, [x25, #0x70]
    stp xzr, xzr, [x28, #16]
    str xzr, [x28, #32]
    str wzr, [x28, #40]
    b relay_publish
.balign 4
target_address:
    .byte 0,0,0,0,0,0,0,0
