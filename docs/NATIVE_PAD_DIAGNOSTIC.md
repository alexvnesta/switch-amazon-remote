# Native pad diagnostic: diag7

No reboot is needed. Exit FTPD and open **Amazon Remote Native Pad Check**.
Release all buttons, press **A once**, then release everything for one second.
Wait 10 seconds, press **+**, and reopen FTPD. If refused or a receipt write
fails, photograph the screen and return to FTPD without retrying.

This tests only a neutral virtual-controller attach/detach in a real native NSO
process, not in the control NRO's applet/application context. No Bluetooth,
remote pairing, navigation input, controller-list replacement, PSC registration
or global boot configuration changes are performed. Success would establish
service replies, not remote support or visible navigation.

## Why this revision exists

The preceding startup test reached a healthy, updating native module status.
Its subsequent neutral-controller test returned kernel `0xD401`, description
106 (`InvalidMemoryState`). The prior work buffer was a page-aligned member of
a backend instantiated on the main thread's stack. Alignment does not make
stack memory transferable. libnx's HDLS attach calls `tmemCreateFromMemory`,
which calls `svcCreateTransferMemory`.

The revised shared backend allocates one aligned 4KiB page. Both the full module
source and this diagnostic establish a real 2MiB kernel heap with
`svcSetHeapSize`; allocating from the old static fake heap is not accepted as
proof of a kernel heap. The backend queries its own memory before HDLS attach
and requires an unshared, writable, page-aligned allocation fully covered by a
Heap region (type 5). Other memory types, locked attributes, short regions,
allocation errors or query failures stop before any transfer-memory submission.

One successful release permits freeing the page. If transfer creation/cleanup
is ambiguous, that page is conservatively quarantined until process exit rather
than recycled. Failure remains latched; there is no background retry.

## Admission and evidence

The new candidate ID is `010000000000a4d1`, outside the application range. It is
staged without a boot flag. Its permissions are limited to `sm:`, `fsp-srv`,
`set:sys`, `hid:dbg` and the existing minimal syscall/SD permissions. Both old
modules, their flags, the passive observer, and prior diagnostic apps stay intact.

Only an explicit A edge after neutral input, followed by a complete neutral
second, permits a launch. The launcher requires confirmed process absence,
exact candidate length/SHA256, and no previous/partial evidence. It exclusively
persists and reads back a launch intent and a neutral-only permit before calling
PM with storage None and zero flags. The native process itself consumes that
fixed-ID, checksummed permit only within two seconds, with a durable used marker
before any HID service call. It attempts one neutral attach/detach, records the
results, and exits. Firmware admission remains limited to 22.5.0.

Module-owned evidence under `/config/amazon-remote-module`:

- `hdls-diag7-launch.bin`: 104-byte launch intent/result.
- `hdls-diag7-permit.bin` and `hdls-diag7-used.bin`: 40-byte authorization and consumed marker.
- `hdls-diag7-result.bin`: 192-byte native initialization/outcome, including a 120-byte per-step trace.
- Corresponding launch/result `.tmp` files: preserve if left behind.

The trace records buffer allocation, memory type/attributes/permissions, memory
query, service open, session attach, device attach, first/last neutral Set,
detach, release, and freed/quarantined state. Raw Results distinguish which call
failed. Full module source also publishes `hdls-backend.bin` after its operator
neutral check, but that full-module revision is not deployed by this test.

Host sanitizer tests and real SDK compilation are not hardware execution proof.
Await the native receipt before promoting the full module or enabling Bluetooth.
Rollback needs no restoration: no existing device files are replaced. Archive
only this new app/candidate if unused, and retain the diagnostic evidence.

Primary source references (SDK pin used by this build):

- [HDLS attach and cleanup](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/hiddbg.c#L427).
- [Transfer-memory allocation](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/kernel/tmem.c#L10).
