# Atmosphere remote integration design

## Central bond B1 trial

B1 is a bounded functional integration for one configured K7Q3M7, not a general
BLE controller release. It retains the working GATT and HDLS output. The first
explicit connection enrolls once through the native security API, captures peer
encryption material privately, waits for its request-specific completion, and
commits/readbacks the project Device save. A later boot restores that material
and requests existing-key encryption before protected GATT. Failure never falls
back to new pairing or deletes existing bonds.

CEN1 stores only PENC (mask1). The audited central action1 reads peer LTK, RAND
and EDIV, not local reproduction roots. Its binding hash identifies the public
local controller namespace, not root lifetime or complete negotiated key
distribution. General full-key profiles retain their root-provenance contract.
IRK support is deferred because the tested identity was public; ordinary-mode
address behavior remains a hardware question.

The bridge's internal event is consumed and erased under the core lock before
logging, public getters or forwarding. Public IPC exposes only status and fixed
Begin/Cancel requests. Begin checks the kernel PID against the input module.
Peer/connection, native HCI handle, nonce, encrypted state and sixteen-byte key
length are checked. One request context per boot survives timeout/cancellation.

Private storage is a Device save in USER owned by the input module, not an SD
file or Nintendo's Bluetooth savedata. Only explicit first enrollment creates
an exactly absent project save. Unknown/corrupt/mismatched data is not replaced.
The SHA256 digest detects corruption, not privileged tampering. Privileged
software can access this storage.

Synthetic runtime/native-instruction tests do not prove remote-side retention,
live SMP ordering, hardware encryption or reboot reconnect. Physical tests must
establish those. No boot activation, wake, native menu or other models are added.

## Product goal and evidence boundary

An Atmosphere background sysmodule should expose the configured Amazon remote
as a virtual Pro Controller in HOME and applications. A companion homebrew app
is for initial enrollment, enable/disable, mapping and diagnostics, not a required
foreground bridge. Ultimately, pairing through Controllers -> Change Grip/Order
and reconnecting without opening homebrew are desired user journeys. Neither is
implemented by calling the existing A/ZL diagnostic flow "native pairing".

MissionControl is the architectural reference: its public README describes an
Atmosphere boot-loaded sysmodule, native-menu pairing, mapped controller input
and reconnection. Its current public Bluetooth-controller transport is not a
drop-in implementation of our independently built BLE HID transport. Its
unpublished BLE branch is not a dependency or source we can promise to ship.
Reference: https://github.com/ndeadly/MissionControl#usage

The [reuse audit](MISSIONCONTROL_REUSE.md) now includes a local adapter using
unchanged upstream button/report code, and identifies the actual missing native
HID registration path. Diagnostic session budgets are temporary guards, not a
product framework. No native-menu enrollment or observer replacement is enabled
by that adapter. The working HDLS path remains available as the proven fallback.
The [BLE enrollment audit](BLE_ENROLLMENT.md) separates saved BLE-list evidence,
current native HID visibility and native-pairing-window state. It uses existing
getters rather than inventing a page detector or treating a GATT read as bonding.

Navigation14 verified one manually connected K7Q3M7 session and actual HOME input.
Current code already runs input as a native background process; the companion
does not deliver input. It still has temporary candidate IDs, a diagnostic
pending marker and one-shot manual lifecycle. This is not yet a boot-ready
compatibility release or proof of durable bonding.
Navigation15 adds an explicit Y enrollment trial through the public BLE API,
not automatic native-menu enrollment. Request acceptance, same-scope listing,
post-reboot persistence and native HID registration remain separate evidence.

## Components

- BLE session owner: single owner of scan requests, connection lifecycle,
  fingerprint/security checks, standard report discovery and subscriptions.
  Never globally changes controller bonds/radio/filter settings. Any reconnect
  policy must first establish what connection/scan/disconnect work it owns.
- Report decoder/mapping: portable, tested HID parsing and remote model profiles.
  K7Q3M7 is the only device-tested profile. Unknown profiles fail closed.
- Controller output: owned HDLS virtual pad, neutral-first attachment, releases,
  presence monitoring and safe teardown. Native Bluetooth-controller emulation
  via MissionControl is an alternative future design, not currently needed to
  deliver the verified HOME input.
- Passive observer: the currently installed reviewed MissionControl-derived v6
  tap provides bounded exact-target advertisement/connection evidence. Stock
  MissionControl does not provide our extension IPC65000-65004. This dependency
  must be version-pinned and disclosed, or replaced after equivalent public API
  access is demonstrated. Never overwrite stock MissionControl under an
  implication that it is an unrelated add-on with no compatibility consequence.
- Companion: opt-in enrollment/status/stop with process-instance checks. Current
  bounded file protocol is adequate for the next test. Use dedicated IPC only
  when lifecycle/config operations need it; avoid an unnecessary service stack.
- Power lifecycle: neutralize/detach before sleep/shutdown acknowledgement;
  reconnect after resume only after ownership and persisted-bond tests pass.
  Wake-from-sleep is a separate later capability and is not a prerequisite.

## Release gates

1. Sustained input: Navigation15 must demonstrate input after idle beyond90s,
   successful explicit stop, neutralization and no unwanted new requests.
2. Ownership/bond evidence: diagnose persisted pairing and safe target-only
   disconnect/connection recovery. Verify reboot and remote-idle behavior.
   Handle-based disconnect exists in public libnx; this alone does not prove
   that a global managed connection belongs exclusively to this module.
3. Bounded reconnect: state machine with backoff, target/model checks, no held
   input replay, finite retry policy and deliberate failure recovery. Tests must
   include stale callbacks, reassigned handles, service loss and suspend/resume.
4. Native-menu enrollment: inspect public MissionControl/Horizon menu lifecycle
   and integration hooks; implement/test an explicit enrollment trigger. Do not
   claim native menu support merely because HDLS appears as a Pro Controller.
5. Boot activation: stable module identity, documented observer dependency,
   opt-in config, safe disable/recovery, firmware guard and resource soak tests.
   Only then offer a boot2 release archive. Preserve a tested rollback package.

Public releases must separate host checks, device-confirmed models/firmware and
unverified features. No ROMs, keys, bonds, private addresses or user logs belong
in the public repository or release assets. Preserve GPL attribution for reused
public code; keep private device receipts outside the source tree.

## Bond persistence integration, not yet connected to the driver

Current evidence: one owned connection receives remote input and can disconnect
cleanly. The operator input fix reads physical Joy-Cons independently of the
virtual controller's player slot. The I1 read-only check reports matching loader
build metadata for the audited driver. M2 subsequently observed target-bound
native key events of types1,2,8 followed by authentication completion with
success=1 during the existing protected read. The session then disconnected
cleanly. This establishes native authentication success, not retention of keys,
complete negotiated bonding, byte-for-byte runtime integrity or reboot reconnect.

`bond_transaction.hpp` is an off-device transaction policy. Its original tests
use a synthetic in-memory store. `bond_store.hpp` now connects it to a validated
private record and the native savedata I/O component. The integrated tests invoke
that component through a fake SDK, with synthetic keys and real host SHA256.
No runtime entry point calls this path. The production key-event adapter, native
reload, stable root-lifetime binding and provisioning remain unimplemented.
Passing these tests is not Switch storage durability or reconnection evidence.

### Exact legacy record layout, checked off-device

The audited stored driver uses the legacy BLE union, not the modern
Secure-Connections layout. The producer and reload consumer agree on type8 as
four-byte local-encryption reproduction metadata. It is neither a 16-byte
BR/EDR link key nor a local LTK. Treating the generic modern type8 definition as
applicable here would save the wrong record.

`legacy_bond_material.hpp` now provides a small private decoder for that layout.
It accepts a fixed-size, target-matched event only for an explicitly selected
audited layout. It zeroes unused union bytes/native padding, rejects unknown or
combined selectors, clears stale output on rejection and does not allow implicit
copies of its secret-bearing buffer. It has no filesystem, IPC, queue, radio or
runtime caller. All tests use synthetic bytes. Decode success establishes only
framing and normalization, not cryptographic validity or permission to commit.

The native reload wrapper copies a full 28-byte union even for smaller records.
The decoder therefore returns initialized bytes across that entire span. The
audited producer copies28/23/24/4/8 bytes for types1/2/4/8/32; the meaningful
portions are28/23/21/4/7 bytes. The remainder is discarded rather than persisted
as incidental stack/padding data. Synthetic ARM replay checks all256 selectors
at two load bases against the actual stored producer instructions. This is not
execution of the complete Bluetooth stack or a hardware reload test.

Local-encryption reproduction can depend on native local roots. Saving its
four-byte record alone does not establish that it will reproduce the same key
after restart. Do not regenerate, replace or export global roots to address that
uncertainty. Verify the existing native root lifetime and the required direction
of encryption before enabling a persistent reload path.

The integration has three responsibilities:

1. The existing Bluetooth stack handles SMP, encryption and key distribution.
   A narrowly scoped private adapter must supply verified completion and the
   complete negotiated key set for the explicitly enrolled remote. The reviewed
   public paired-device add/get route is not a demonstrated generic BLE key
   export/import route. Do not put BLE material into the classic link-key field.
2. The private store binds that candidate to local controller identity, resolved
   remote identity and address type, profile/schema and generation. It commits
   only after successful pairing, negotiated bonding and completed distribution.
   Request acceptance, encrypted reads, LEDs and button reports are insufficient.
3. Reconnect loads the verified record through the existing lower stack, then
   uses the existing connection, fingerprint, subscription and controller output
   code. It must demonstrate normal-mode reconnect without Home-flashing. A
   local Saved receipt does not prove either peer retained matching keys.

The policy passes opaque references, never key bytes. Its backend must resolve
them privately and reject stale owner/attempt/connection epochs, changed local
identity, incomplete key sets and inappropriate security levels. It must preserve
the distinction between a bonded Just Works connection and MITM protection.
Legacy and Secure Connections key formats are not interchangeable. An IRK is
required only when negotiated/needed; do not invent one or copy another peer's
identity. The default key-length policy is 16 bytes; any smaller supported value
requires an explicit compatibility decision rather than automatic downgrade.

### Storage and failure semantics

Do not use `armodule::Publish` for bonds: its remove-then-rename behavior is
documented as non-atomic. Do not save plaintext keys in FTP-visible SD config,
diagnostic reports or the observer's public IPC. POSIX mode0600 on FAT does not
provide that privacy. The native I/O component targets a project-owned Device
save in USER, but no storage has been provisioned or verified on hardware. The existing Nintendo
Bluetooth system save, controller pairings and user saves must remain untouched.
The presence of libnx `fsFsCommit` alone does not establish access, transaction
semantics or power-loss behavior for a selected filesystem. Those need validation
before enabling the adapter. No filesystem/save ID has been allocated here.

`bond_save_io.hpp` implements the actual libnx open/read/write/flush/commit calls
for one pre-provisioned, fixed512-byte slot. It uses the current input-module
candidate program ID as its exact ownership constraint, not a newly assigned
system-save ID. Inventory is filtered to Device/USER/owner/zero-user/index/rank,
and extra metadata must independently confirm owner, attributes and a journal.
Missing, duplicate, foreign, malformed or inaccessible storage fails closed.
There are no storage creation/deletion, system-save, raw-partition, SD fallback,
file truncation, rename or delete calls. The final owner and a separately reviewed
provisioning step remain release prerequisites; this is not a ready-to-install
storage feature.

Replacement compares every prior byte, writes only the existing slot, flushes,
verifies the staged bytes, commits once, closes, then reopens read-only for full
readback. A pre-commit error closes the filesystem and checks the old record
read-only. It does not assume close undoes changes and never rewrites old bytes
as a recovery operation. Any commit error is ambiguous and stops without retry.
A failed readback after commit also requires recovery. Buffers internal to the
adapter are cleared, and receipts contain only numeric status, step and Result.
The integrated store also supplies an owner/deadline guard before opening,
immediately before the write and immediately before commit. The driver must
serialize callbacks and hold the owner/writer lock throughout the transaction.

This I/O layer does not invent a bond record format, validate pairing evidence,
authenticate its caller or trigger native key reload. The private record adapter
must provide validated schema, stable identity, profile and generation, complete
negotiated key material and one serialized owner across the entire transaction.
Read-only recovery bytes are private and must be validated and cleared by that
caller. Do not pass SD command data directly into this interface. Private here
means outside the FTP SD mount, not inaccessible to privileged homebrew.

Tests invoke the actual I/O adapter through a narrow fake SDK. They cover all23
Result-returning calls in its success path and all8 in its recovery getter, plus
partial writes, commit-error-with-data-saved, missing/duplicate/foreign saves,
ownership changes at readback, readback corruption, stale expected bytes,
one-shot consumption, eight double-failure recovery cases and handle cleanup.
Separate compilation against the installed real libnx headers checks the target
API/types and the object's undefined-symbol boundary. These are not native FS
journal/power-loss, save access, real bond storage or reconnect tests.

### Canonical record and transaction bridge, checked off-device

`bond_record.hpp` defines a private 512-byte schema with explicit empty/bonded
states. It binds the record to the candidate project owner, local and resolved
peer identities and address types, profile, generation and a trusted stable
controller-context token. It records the negotiated key mask, length and security
level, then five fixed canonical legacy slots. Unused slots and padding must be
zero. Missing or corrupt data is never interpreted as a fresh empty store.

The final 32 bytes are a SHA256 corruption check, using the native
[libnx SHA256 API](https://switchbrew.github.io/libnx/sha256_8h.html).
This is not encryption, a MAC, authentication or durable anti-rollback protection.
The in-session generation check rejects a stale expected generation, but cannot
detect an attacker restoring a whole valid old save. No digest or key bytes are
included in public receipts, SD config, logs or IPC. Buffers are noncopyable and
cleared after use; this does not erase incidental registers or crash artifacts.

Legacy encryption/signing field positions are consistent with the pinned
[public Bluedroid contract](https://android.googlesource.com/platform/external/bluetooth/bluedroid/+/5738f83aeb59361a0a2eda2460113f6dc9194271/stack/include/btm_api.h).
The older Bluedroid identity slot is only an IRK. The audited 23-byte identity
variant instead matches the identity/type/address structure in this
[public BSA header](https://github.com/rubikpi-ai/rubikpi_btapp/blob/cf7a0be25b0419561cd53d63e52e8f2dc4db56cf/source/src/linux/libbsa/include/btm_api.h).
These are interoperability references, not imported implementations or proof of
the Switch's runtime field values. The record validator conservatively checks
these fields and rejects unsupported security values. It requires the peer
encryption slot for the planned central reconnect, and an exact negotiated mask,
not a mask inferred by accumulating whatever callbacks happened to arrive.

`bond_store.hpp` stages the next canonical record in private RAM, verifies it,
then performs the native replacement transaction. It compares the exact previous
generation, refuses a silent reduction of saved key length/MITM protection, and
rechecks the live owner and deadline at the storage boundaries. A Saved receipt
requires full readback after commit. Unknown commits stop without retry or undo.
Abort is a read-only check, never a rewrite of an old bond. Neither result says
what keys the remote retained or whether native reload/reconnect works.

The integration test injects failure at all39 native calls in its successful
path, tests12 owner/deadline interruption points, and covers partial writes,
corrupted readback, security/generation mismatch and ambiguous commits both
before and after data becomes durable in the mock. Codec tests corrupt every
byte position, reject truncated/oversized records, rehash malformed semantic
fields and exercise all16 supported optional key distributions. macOS host
tests use CommonCrypto; Linux uses OpenSSL. The target uses libnx, not either
host library. Target compilation checks real SDK declarations and emitted native
dependencies, but is not a console executable or hardware persistence test.

Before deployment, the private driver must supply complete negotiated evidence,
bind the candidate bytes and identities to the exact owned enrollment, establish
the native controller/root lifetime, implement validated reload, and provision
only the approved project save. Do not substitute a process ID or fabricated SD
token for that lifetime binding. Do not replace/export native global roots or
modify Nintendo's Bluetooth save. No repeat of the completed M2 test is needed
for this off-device work.

### M3 scalar-property device check

M3 is a diagnostic prerequisite for connecting the private store to live pairing,
not persistent bonding. The M2 trace established authentication success and key
types, but did not establish the actual length/security values or whether the
delivered identity is the same identity used in the configured target. M3 adds
only those scalar fields, a target-equality boolean and the authentication failure
reason. It uses the same bounded 15-second nonce window and existing P3 Link and
cleanup path. No additional pairing API or security request is added.

The exact-build driver hook reads selected metadata bytes. It never reads LTK,
IRK, CSRK, DIV, RAND, EDIV or classic link-key fields. Unknown selectors have no
property reads. Native queue/relay behavior and the original callback continuation
are preserved. Its 32-byte packet extends the M1 prefix, retaining the old getter
and providing a separate fixed M3 getter. M3 refuses the old 16-byte probe reply.
The public report contains only scalar metadata, no keys or peer identity bytes.
Unknown security values are recorded for review, never accepted as a valid bond.

The test does not create a private save, reload keys, reset controllers or enable
boot-time input. Saved-pairing, complete negotiated distribution and ordinary-mode
reconnect remain unproven. The current stopped session must stay intact while
staging. A user reboot is required before preparing the next session; do not
remove its pending marker while its input process is still running.

The completed M3 hardware run observed peer encryption and legacy local
encryption metadata with key length 16 and security value 1, a public peer
identity matching the configured target, and native authentication success with
reason 0. The existing input subscription and clean disconnect paths also
completed. That closes the scalar-property check. Do not repeat M3 to obtain
already-observed evidence. It does not establish the negotiated distribution,
durable storage, root lifetime, native reload or remote-side retention.

### Private candidate collection, checked off-device

`bond_candidate.hpp` connects normalized legacy callbacks to `bond_store.hpp`
for one owned enrollment. It is a private component with no production caller,
not another SD diagnostic or a deployable pairing implementation. The caller
must serialize callbacks, cancellation and persistence under its enrollment and
writer lock. It must independently provide the negotiated distribution, native
completion/encryption evidence and a trusted stable controller-root context.
Never manufacture those inputs from M3 success or the set of keys seen so far.

The collector admits only the audited legacy layout and approved, fingerprinted
target. It ignores foreign owners, peers and enrollment completions, rejects
malformed, duplicate, unexpected or incomplete target material, and checks the
clock and live context throughout. Successful completion must agree with the
original negotiated mask, length and security. The candidate is consumed before
the one-shot private transaction begins, and retained key buffers are cleared
on rejection, cancellation or any storage outcome. No key getter, logger, public
IPC, SD key file, radio request or automatic retry was introduced.

This initial contract requires the callback address to equal the resolved peer
identity. It is not a general RPA resolution implementation or Secure Connections
adapter. Timeout and disconnect cleanup require explicit Poll/Cancel calls from
the future driver adapter; this class does not schedule timers by itself.

Synthetic tests cover all 16 supported distributions, all 261 key-event orders
under both supported security levels, key lengths 7 through 16 with explicit
policy, invalid/foreign/duplicate/early events, context loss, clock changes,
generation/security downgrade rejection and all 39 native-call failure points.
They verify that no I/O occurs before explicit completed-candidate persistence,
that ambiguous commits are not retried, and that retained candidate buffers are
cleared. Real SDK compilation checks the native ABI and dependency boundary.
None of this is a real-device persistence or reconnect test.

The remaining production work is native event/completion wiring, trusted
negotiated-distribution and root-lifetime provenance, project-save provisioning
and validated target-only reload. The working M3 installation, completed session
and pending marker remain unchanged. No new physical test is ready from this
component alone.

The adapter contract is Begin (compare prior generation), Stage, VerifyStaged,
Commit, then VerifyCommitted. Verification includes full private record bytes,
schema, candidate and identity binding; a nonzero token is not proof. The final
owner check and commit must be serialized against disconnect/replacement events.
A definite pre-commit failure can abort the staged transaction while retaining
the previous record. An ambiguous commit or failed committed readback requires
read-only recovery. Never delete, repeat enrollment or automatically reconnect
to guess which generation won. Even a retained older host record may no longer
be accepted by a remote that already completed fresh pairing.

On startup, validate a committed record before any native reload. A driver
restart invalidates loaded-state receipts. Reload must be target-only, and its
success is distinct from peer authentication on the next connection. Existing
bounded scan/ownership checks remain until that path is device-tested. No marker
deletion, boot activation or unlimited retries are implied by this design.

### Reuse and the next implementation boundary

Zephyr's Apache-2.0 SMP implementation is a behavioral reference for saving keys
after successful negotiated bonding, distinguishing Just Works from MITM and
using different legacy/SC key metadata. Its storage/host code is not a drop-in
Switch driver. No source was copied. Publicly hosted BSA sample files examined
for the audit contain proprietary notices, so they are not treated as reusable
open-source implementation material.

The next production component is the private driver adapter, not another
application-only Bond request. Before deployment it needs a version-bound
integration/rollback review, exact runtime compatibility checks beyond I1's build
metadata, tested event normalization and target isolation. Its diagnostics must
report only event/result/key-kind metadata, never raw secrets. It must leave
other controllers' events and state unchanged. M2 already observed security
completion and M3 observed scalar properties; do not repeat those tests. Complete negotiated distribution, private
commit, native reload and ordinary reconnect still need their own evidence.
No new console test or deployment is needed for the off-device decoder alone.

### Selective native reload preparation

A further stored-image audit found that the existing bulk BSA add-device
consumer has one BLE-key-present flag, not a per-kind presence mask. When that
flag is set, it submits all five legacy key kinds unconditionally. That is not
suitable for the three-kind set observed in M3. Zero-filling optional signing
slots would still submit those slots. Its reused 28-byte scratch union also
retains unused tail bytes between smaller structures. This is a framing and
integration concern, not a claim of an exploitable flaw.

Synthetic replay checked this behavior for all 16 key masks at two load bases.
The compiled C++ bridge also emits 320 synthetic record cases for replay through
the stored per-key wrapper. That checks its fixed target, selector and fully
copied 28-byte union, plus its no-submission allocation-failure path.
The native allocation, device database and queue calls were stubbed. Neither
the bulk acknowledgement nor successful queue submission proves that a key was
accepted by the lower stack.

`legacy_bond_reload.hpp` prepares a selective private reload from a validated
committed record. It snapshots and checks the entire record, exact trusted
binding/generation/security policy and audited layout, then submits only the
present normalized slots through a private caller-supplied sink. An M3-shaped
set submits kinds 1, 2 and 8, never invented kinds 4 or 32. Each union has its
own zero tail; no scratch material is reused. Live ownership is checked before
each submission and once after the last. The one-shot snapshot is cleared on
every outcome; errors stop without retry, bond deletion or automatic pairing.

The result is named Submitted, not Loaded. A private native sender and a
verified completion/readback barrier are still missing. No runtime caller,
IPC importer, save provisioning, native hook or connection request was added.
The interface requires synchronous copying by its sink and serialization by
the caller. Tests cover all 16 masks, both supported security levels and 10 key
lengths, corrupted/empty/stale/foreign records, ownership loss, partial/ambiguous
submission, immutable snapshotting and clearing. This is not device reload or
reconnect evidence and does not change the installed M3 test.

The older [AOSP BLE implementation](https://android.googlesource.com/platform/external/bluetooth/bluedroid/+/5738f83aeb59361a0a2eda2460113f6dc9194271/stack/btm/btm_ble.c)
distinguishes central-side use of the peer LTK from local key reproduction.
That suggests a possible later simplification for this client-only profile,
but does not establish the exact Switch path or remove our root-lifetime gate.
No native global roots were read, changed or regenerated. No upstream source
implementation was copied for this component.

### Native restore consumption and confirmation boundary

The next pinned-image replay follows the queued per-key message through the
native dispatcher, address lookup and actual cache-store instructions. Unlike
the earlier wrapper-only replay, the lookup and key writes are not stubs.
The cache, peer addresses and all material are synthetic. Allocation, queue
delivery and message release still use stubs; this is not a running Bluetooth
stack or an on-console reload test.

The native restore consumer intentionally disables the application key-event
callback. Its lower boolean reports admission of the selector, pointer and
existing address entry. The outer dispatcher discards that boolean and returns
a disposition to release the message, including on rejected submissions. A
pairing/key callback or a freed message therefore cannot confirm restoration.
The lookup itself only checks allocated state and address, not the caller's
trusted address type, device type or driver epoch. Ownership and serialization
must be independently established by the eventual private adapter.

Two local record types also share one native DIV field. `bond_record.hpp` now
requires LENC and LCSRK to agree when both are present, at both construction and
inspection. A contradictory record is rejected before storage writes or reload
submission, even if its checksum is valid. Peer EDIV is a separate value and is
not compared with local DIV. The common encryption security/length fields were
already constrained by the record policy. No deployed record migration is
needed because there is still no production writer for this private format.

Tests run the 320 compiled synthetic distributions/security/length fixtures at
two bases and three cache positions, checking each native key write and all
neighboring records. They cover every invalid byte selector, absent/inactive
entries and null material. Deliberately removing the synthetic entry between
the consumer's two lookups demonstrates that its admission boolean alone is
not a readback guarantee. This injected fault is not evidence of a race in the
stock serialized stack. A nonzero callback trap verifies that restore generates
no application key event.

The implementation boundary remains unchanged: the native sender, serialized
target-only confirmation, trusted capture/negotiation/root provenance and save
provisioning are not wired into the installed plugin. Future confirmation must
check the owned entry and all expected fields after the final selective write,
privately on the native stack's serialized path. It must report metadata only,
not expose a general cache/key reader, and must not reconnect merely because
the request was queued or the lower call returned true. No driver patch, live
key read, root replacement, deployment or new physical test is part of this
audit.

The older [AOSP per-key action](https://android.googlesource.com/platform/external/bluetooth/bluedroid/+/5738f83/bta/dm/bta_dm_act.c)
and BLE store provide corroborating architecture, not an exact Switch ABI or
copied implementation. Exact conclusions above come from the pinned-image
instruction replay.

### Private field confirmation and central reconnect ordering

`legacy_native_confirmation.hpp` implements the comparison portion of restore
confirmation. It validates the committed record against independently supplied
binding/generation/security policy, then compares one exact-size, BLE-only
native entry. Allocated state, peer address/type, exact key mask, key-known and
MITM flags, and every meaningful field of each present key must match. It
checks live ownership before and after comparison and returns only a boolean.
There is no native lookup, address traversal, key getter, IPC endpoint, savedata
access, or production caller. Unrelated connection state and native padding are
not mistaken for key material. The trusted caller must serialize the complete
comparison with native updates and hold the immutable expected record; the
function itself does not acquire that lock or establish ownership.

Tests exercise 320 synthetic distributions/security/length combinations and
21,120 native-field mutations, plus malformed record/entry sizes, stale and
foreign bindings, missing flags and ownership loss. The compiled C++ program
emits only hardcoded synthetic fixtures, with no file or console input. The
private audit compares those exact successful comparison inputs with actual
native lookup/store output at two bases and three cache positions. A true
comparison is not by itself a completed native batch, an encrypted link,
durable peer retention or permission to reconnect.

A separate replay now traces the central existing-key encryption branch and
its HCI command constructor. With only synthetic PENC loaded, it constructs
the encryption request from peer LTK/RAND/EDIV. The replay forbids all native
global-root reads and all other peers' key reads. Missing entry/key, allocation
failure and an already-pending request do not produce a new HCI submission.
Actual controller transport and SMP are stubs, so a command-started return is
not encrypted-link or hardware-reconnect evidence. This supports a central-only
reconnect design without restoring or replacing global roots. It does not
remove the existing full-record root-lifetime check: those records still
contain local reproduction material, and a narrower peer-only format/policy
has not been implemented.

The public GATT auth value and the lower security action are different enums.
In this exact stored image, a NoMitm GATT read on an unencrypted entry selects
the SMP-entry route even when PENC is loaded. An encrypted entry instead selects
the already-satisfied route; a pending encryption selects wait. The audit stops
at the SMP entry, so it does **not** establish that SMP would replace an old
key or perform fresh pairing. It must not be described as the cause of a prior
device failure. The existing first-enrollment NoMitm read is unchanged.

The intended saved-bond sequence is now: privately restore and confirm the
owned entry, request existing-key encryption on the owned central link, await
target-specific encryption completion, then allow authenticated GATT reads and
input subscription. No authentication downgrade or automatic pairing fallback
is part of it. Wiring this sequence into the native driver, along with trusted
capture/negotiation and private-save provisioning, is still unfinished. No new
device build, root/key access or physical test is provided by these components.

The [Bluetooth security specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html)
distinguishes the peer-provided encryption material used by a central from
keys distributed for reversed roles. Public [AOSP GATT policy](https://android.googlesource.com/platform/system/bt/+/64d3ed3c9dc4ef0060519f9e9081e72780f9be9e/stack/gatt/gatt_auth.c)
and [AOSP encryption control](https://android.googlesource.com/platform/system/bt/+/258c2538e3b62a8cdb403f2730c45d721e5292b4/stack/btm/btm_ble.c)
provide architectural context, but their enum/branch behavior must not substitute
for the exact stored-image checks. No upstream implementation was copied.

### Reuse the native encryption callback

The audited native encryption API already accepts a completion callback and
opaque reference. A separate global encryption-event hook is not needed for
this request. The reference also begins with the legacy security-action byte,
which must remain valid until native completion. Existing-key action1 and LE
transport2 are distinct from the public GATT authentication enum.

`legacy_encryption_request.hpp` now implements the private one-shot caller.
It arms callback state before making the native call because errors and an
already-satisfied request can complete synchronously. Return1 means pending,
not encrypted. Delayed success uses the native callback, then fresh trusted
ownership, encrypted-state and key-policy checks before authorizing GATT once.
Timeout, cancellation, wrong peer/transport, ownership loss and failure cannot
be revived by a late callback. No retry or new-pairing fallback is added.

The native callback carries address, transport, the original reference and
status, but no connection epoch or HCI handle. The production backend must
independently validate those and the verified-restoration generation. It must
also confirm central role, effective key length and required MITM protection.
The native request and callback execute under stack serialization. A pending
callback reference is not released by a local timeout or cancellation, so both
request and backend require driver-lifetime storage. This helper has no rearm
or reset operation and never overwrites native callback slots itself.

Private instruction replay runs the stored request API, native address/handle
lookups, encryption-change handling and callback dispatch. Delayed success
clears the native callback slot before invoking it, then notifies GATT. A busy
request retains the other callback. No-key, no-entry, no-ACL and allocation
failures do not submit encryption work. Duplicate and foreign-handle events
do not complete the request twice or finish the wrong connection.

The actual AArch64-compiled C++ caller and callback also execute against that
stored code at two bases and three synthetic cache positions. The 78 integrated
cases cover delayed/inline success, inline failure, late callbacks, ownership
and encryption loss, failed encryption and foreign handles. Ownership/time
admission and encrypted-state queries remain explicit harness boundaries;
HCI transport, SMP notification, GATT wakeup and pending-security work are
stubbed. These are not radio, whole-stack concurrency or hardware tests.

There is still no production backend, installed caller, private key channel or
save provisioner. The existing observer's public metadata queue must not be
repurposed to expose bond keys. Installed M3 and its first-enrollment flow are
unchanged. This code supplies no new user test and does not establish saved
pairing, boot reconnect or remote retention.

References: [Zephyr v4.2.0 SMP](https://github.com/zephyrproject-rtos/zephyr/blob/v4.2.0/subsys/bluetooth/host/smp.c),
[libnx filesystem API](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/fs.h),
[libnx filesystem wrappers](https://github.com/switchbrew/libnx/blob/master/nx/source/services/fs.c),
[MissionControl public architecture](https://github.com/ndeadly/MissionControl).
