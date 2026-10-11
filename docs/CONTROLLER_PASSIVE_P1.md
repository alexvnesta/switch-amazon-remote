# Passive capability research: P1 comparison and D1 integration

Not cleared for deployment. D1 now has a locally built native integration,
matched companion and foreground reader. Loader/startup qualification, fresh
console inventory and recoverable staging remain open. No P1 or D1 candidate
has been uploaded. Q1 and Q2 remain withdrawn. Do not repeat their tests.

The direct startup-queue producer is now a retained comparison candidate,
not the preferred integration path. Work is moving to an independently owned
deferred record. Its capture core, borrowed-envelope adapter and fixed record
readout are tested in a separately linked source-owned image. D1 binds that
image and a late metadata transport in a private offline package. The earlier
P1 companion/NRO pair does not read this slot.

## What this review established

The selected retained driver has a conditional capability request. The
selected dispatcher event is 2; a feature flag can skip the request entirely.
Its existing completion callback is a no-op. That callback receives a borrowed
stack envelope containing an opcode, length and response pointer. Neither the
envelope nor the response pointer may be retained after return.

The candidate copies only a status byte or the nine-byte capability prefix.
It makes no radio request. Unknown response extensions are not copied. A
dedicated 32-byte event is decoded separately from ordinary controller traffic.
Missing data means **not observed**, never **unsupported**.

The native queue copies the supplied bytes before returning and has failure
returns for inactive/full states. It uses a mutex. The core consumer defers the
next dequeue while its mailbox is occupied. Publishing also requires the native
mailbox event to be initialized. A further exact-artifact audit identifies its
creation in a `DT_INIT_ARRAY` initializer, checks the failure branch, and
identifies mailbox size and mutex storage initialization. These bounded static
findings are not a proof of complete startup ordering, lock ordering or
successful delivery.

A further delivery review identifies another lock inside event signaling.
The selected native queue peek/pop paths do not acquire the producer mutex,
but that does not prove the complete lock graph. The companion's global driver
session is opened after its enable signal, so simply moving event reads ahead
of interface initialization is not a safe fix either. These are local source
and pinned-image findings, not an observed console deadlock.

## Deferred capture replacement, still offline

`tools/driver-capability-p1/deferred_capture.hpp` implements a 36-byte owned
record with empty, writing and published states. The callback gets one weak
atomic claim attempt. On contention or a spurious failed claim it drops that
observation without retrying or reading its payload. A claimed observation
copies at most one error-status byte or the nine-byte valid capability prefix,
then release-publishes an immutable record. Incomplete zero-status replies,
invalid envelopes and invalid filtering flags release only the unpublished
claim. A later acquire-reader returns empty, busy or ready; it
never waits for the writer. There are no queue, service, mutex, timer or radio
calls in this component.

This changes the evidence contract: **only the first complete capability prefix
or explicit controller error is retained**. Later responses are neither compared nor counted.
It is not the companion's 64-event history. Empty does not establish that no
response occurred; busy does not authorize resetting or reusing the storage.
There is no reset, overwrite or retry API. Releasing an incomplete, unpublished
claim is not a reset of a published record. Firmware resets must not make this
historical record appear current. The native dispatcher has a one-byte status
path as well as a complete-reply path; a test verifies that a zero-status-only
observation cannot hide a later complete reply. Actual occurrence on this
console has not been observed.

The host tests exercise all 65,536 declared lengths, all nonzero one-byte
statuses, null/unselected payloads, immutable retention and concurrent access.
`test_deferred_arm.py` compiles only our source at invented addresses and ran
65,808 guarded synthetic calls. For that generated build, a control-flow
audit finds no cycle, external call or undefined helper, with upper bounds of
64 capture instructions and 11 read instructions. It checks failed exclusive
store, busy/ready refusal, publication order and ABI preservation. These are
instruction counts, not device latency measurements or a hardware memory-model
test. Target builds must use and verify the reviewed non-outlined atomics.

### Source-owned image and readout

`deferred_endpoint.hpp` adds the explicit 16-byte borrowed callback envelope
and a fixed 64-byte readout. It selects the capability opcode before reading
the payload. A readout contains the first-record policy, empty/busy/ready state,
and a validated event, not a pointer. Empty/busy records have zero event bytes.
Null, misaligned or wrong-sized output buffers are rejected without writes.
The receiving decoder rejects unknown formats, reserved bits, noncanonical
empty/busy records and invalid capability data. Failure leaves its outputs
unchanged. This is deliberately distinct from the older 112-byte cache format.

The separate source-owned ELF fixture has RX code, read-only format metadata,
and a 36-byte initialized RW slot. Its linker rejects additional mutable
storage, runtime relocations, constructors and dynamic-linking dependencies.
It has no process-startup implementation, NSO packaging, native addresses,
firmware code or installer. It is **not** a drop-in Switch module.

The final fixture passes 133,080 guarded ARM calls across three invented load
addresses. Tests cover all declared 16-bit lengths, all unselected 16-bit
opcodes, all nonzero status bytes, incomplete acknowledgments, failed weak
claims, immutable publication, empty/busy reads and invalid output sizes or
alignment. For this generated build, acyclic control-flow upper bounds are
68 instructions for the envelope adapter/capture and 35 for readout, with no
external calls or undefined helpers. These remain synthetic checks, not device
timing, loader integration or hardware memory-model proof.

### Storage and transport constraints

Atmosphere 1.11.2's [patch loader](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/loader/source/ldr_patcher.cpp)
protects the NSO header. Its [process loader](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/loader/source/ldr_process_creation.cpp)
calculates allocation before applying IPS patches, initializes declared
segments and then sets RX/R/RW permissions. An IPS patch cannot add a new
allocation. A supplemental source-owned image is a candidate, but the actual
module inventory, possible argument gap, native runtime handling, resource
cost and matched rollback still need qualification. These are source findings,
not verification of the loader currently running on the console.

A further pinned native review checks 47 startup instructions. The selected
BSS clear stops before the end of the original main image. Startup also probes
that end address for an argument area, but only parses it when `QueryMemory`
reports **read/write permission**, not merely a particular memory type. An
RX-first supplemental image would skip this selected parser if the proposed
layout holds. A storage-only RW-first image is therefore not an equivalent
alternative. This conditional static result does not qualify all runtime
initialization, a live layout or a whole-console boot.

Never bind a native hook to a supplemental image that might be absent or
mismatched. Any eventual packaging must keep the native changes and owned
image a matched unit, preserve program identity/permissions and existing
normal-input changes, and remain recoverable. No firmware-containing package
may be added to the public repository.

The existing custom-command carrier is input-only `WriteHidData2` IPC 20.
It cannot directly return this 64-byte record. Writing into its input buffer
is not a supported output contract. A separate fixed output adapter or an
explicitly requested, correlated late metadata event needs qualification.
The latter would only copy the already-captured record after interface and
driver-session readiness, never publish from the startup callback or issue a
radio command. D1 implements the latter offline.

Guessed BSS padding, pairing buffers and changes to startup event-draining
order are not acceptable substitutes. D1 uses a declared initialized slot in
its own supplemental image. The older P1 reader is not interchangeable with
the D1 reader; neither is an end-to-end tested console release.

## D1 matched offline integration

The own-source supplemental image contains the capture/getter and three
adapters. Capture does not use native queues, IPC, locks or radio APIs. One
fixed 16-byte late request returns an 80-byte nonce-correlated metadata event.
Unknown requests and unrelated events retain their existing routes. The
incompatible legacy generic bridge is refused; normal HID uses its correct
native stack frame. Saved-peer specialized routing is retained.

The linked adapters pass 3,429 guarded synthetic calls at three load bases.
The tests were run with invented bindings and again with the private candidate's
exact bindings. Only our code executes; native helpers are stubs. The tests
caught and corrected conditional branches exceeding their address range.
They check queue return propagation, ABI preservation, invalid request/event
refusal, fixed mailbox writes, normal HID fallback and unrelated traffic.
They do not establish a bound on real mutex waits or successful console boot.

The private package keeps the transformed main, original NPDM and own-source
supplemental image together. Existing normal-input patches are baked into the
matched main. The declared text extent includes the formerly patched padding,
so the loader does not zero it. Distinct content-derived module IDs prevent
the original build-ID-specific IPS from overwriting the new hooks. Original
RO/data bytes, program identity and permissions are preserved. The extra
allocation is three pages. Firmware-containing files are never public assets.

The companion waits for global driver-session readiness and for all event
holders to be linked. An explicit command 65021 admits at most one read per
companion lifetime. An existing asynchronous worker sends the fixed request
without holding the metadata mutex. Command 65022 copies a 144-byte status.
There is no automatic startup request or retry. The state machine handles a
reply arriving before the send returns, queue rejection, send failure,
malformed/mismatched replies, backwards time and expiry. Terminal evidence is
immutable. Expiry does not cancel synchronous driver IPC.

The new foreground reader checks exact expected firmware, driver/companion
module IDs and the driver's relative module layout before requesting metadata.
Those are loader metadata checks, not runtime integrity attestation and not
protection against an earlier boot failure. It saves verified checkpoints and
an identity-match marker before Begin, makes at most one Begin and at most 120
getter polls, then exclusively writes a validated 176-byte report. Partial or
failed files are retained. Unknown reply bytes are not persisted. The display
has one action and a separate colored status. Its build requires a generated
manifest tied to the exact private package and companion; no default target
or public installer enables it.

## Offline companion and reader integration

The isolated companion build consumes the private event before logging or
forwarding it to Horizon. A mutex-protected cache retains at most 64 attempts,
the first decoded observation, counters, timestamps and conflict/error flags.
Its getter, command 65020, only copies this cache. It never asks the Bluetooth
driver for new data. Malformed private events are consumed, not forwarded.
The getter takes only the cache mutex; the event path takes the core event
lock before the cache mutex. Native callback/queue lock ordering is a separate,
still-open review.

Snapshots describe historical observations since the cache was initialized
in this companion instance, not current radio state or a controller reset
epoch. Conflicting, rejected, clock-invalid or exhausted observations cannot
be flattened into a clean successful checkpoint summary. Complete validated
snapshots retain those flags. The cache has no reset or retry operation.

`patches/missioncontrol-passive-capabilities-p1.patch` describes the isolated
integration. In that candidate, the three legacy generic bridge methods
(`GetHciHandle`, `SendHciCommand`, `DmSetConfig`) return `BadInput` without
sending or waiting. This is not a change already installed on the console.
Normal input and power/lifecycle paths are unchanged in this patch.

The matching foreground reader opens only the companion service and reads
the cached snapshot once. It does not initialize Bluetooth, pair, connect,
reset, change filters or send HCI commands. Its explicit build target is
`build/amazon-remote-passive-reader.nro` under `tools/amazon-remote-module`;
it is excluded from the default build and is not a console-test release.

## Offline checks

- Strict event decoder tests cover all native 16-bit lengths, malformed schemas,
  reserved fields and prefix tails.
- The actual assembled candidate ran through 66,063 synthetic calls, checking
  guarded payload reads, stack/register preservation, queue success/failure,
  normal-HID fallback framing, unrelated event continuation and mailbox bounds.
  Native queue, lock and continuation functions were stubs. No Nintendo
  firmware was executed by this test.
- The private exact-artifact static audit checks 89 instructions, selected
  pointers/jump-table behavior and rejection of changed artifacts. It emits
  assertions, not firmware bytes or a patch.
- An additional static startup audit checks 25 instructions and the initializer
  table. It does not execute firmware or establish complete callback ordering.
- The actual cache/runtime passes concurrent producer/reader tests with address
  and undefined-behavior sanitizers and a minimal SDK stub. The isolated
  companion and matching reader compile against the target SDK.
- Reader tests cover every checkpoint failure boundary, failed/invalid replies,
  conflicting observations, record corruption, exclusive real-file I/O,
  short writes, failed synchronization and orphan snapshot refusal.
- The checkpoint/decoder code compiles against the target SDK with the same
  POSIX feature declaration used by the application build.
- Host regression, publication hygiene and release-inventory tests pass.

The synthetic ARM harness is `tools/driver-capability-p1/test_observer_arm.py`.
Supply an AArch64 bare-metal toolchain using `--prefix`. It uses invented symbol
addresses, compiles only our source, and requires Unicorn. Its pass result does
not authorize substituting native bindings or installing the candidate.

## Durable diagnostic checkpoints

`observation_checkpoint.hpp` and `observation_checkpoint_io.hpp` implement a
small journal used by the foreground reader:

1. Prepared.
2. Before read, saved before opening the service or reading its cache.
3. Read returned, retaining the transport result.
4. Finished, including validated metadata only.

Schema 2 binds each immutable record to a run, reader context, expected
companion manifest, stage, previous checksum and timestamp. The reader context
is the foreground process ID, not a boot identity. The writer creates exclusively,
synchronizes, closes and reads back. Existing or partial records block a new
run in that directory. A write failure latches failure; nothing is deleted or
overwritten. A read-only inspector reports the verified prefix and rejects
gaps, changed identity, truncated records and corrupted links.

The checksum detects accidental corruption, not forgery. The manifest is the
expected companion artifact hash, not runtime attestation. The run and reader
context are correlation identifiers, not independent boot provenance. Readback is
not a guarantee of FAT32 power-loss durability. A file left after failed fsync
can later be readable; the file alone cannot prove that fsync succeeded.

A `BeforeRead` record without `ReadReturned` only establishes the last saved
boundary. It does not establish that IPC started or that a command reached the
controller. **These foreground checkpoints do not cover a passive callback
running earlier during boot.** No such coverage is claimed.

The getter uses synchronous service IPC without a hard deadline. A saved
checkpoint makes its last completed boundary inspectable; it does not prevent
an IPC stall or make the native producer safe. `snapshot.bin` is created
exclusively, synchronized and validated after exact readback. Partial files
are retained and block reuse of that run directory.

## Gates still open

Native mailbox creation has been located and its failure check reviewed, but
the retained direct-queue route lacks complete producer/consumer lock ordering
and early-delivery qualification. It must not be installed. Companion event
handling waits for core, HID and BLE initialization; early diagnostic traffic
must not obstruct normal initialization. This is an unresolved risk, not an
observed deadlock. The replacement capture core avoids this queue entirely;
the D1 late read does not publish anything from the startup callback.

The D1 native package, companion and foreground reader are built offline,
but no end-to-end device evidence exists. Actual loader ordering and the
absence of an inserted argument area must be qualified before installing its
fixed relative bindings. Full native startup is not proven by selected static
checks. Current firmware and all active patches need a fresh preflight, and
the native override needs a reviewed recoverable staging/rollback procedure.
The conditional stock response may not occur. The stored metadata must not be
treated as a fresh controller-state snapshot across resets.

The candidate's legacy refusal route would disable generic custom commands;
that behavior change must be reviewed separately against all callers. It is
not installed and is not represented as a fix already on the console.

Even a successful capability report would establish only the reported filter
metadata. It would not prove wake-filter lifetime, Home-specific traffic,
interrupt delivery or deep-sleep wake support.
