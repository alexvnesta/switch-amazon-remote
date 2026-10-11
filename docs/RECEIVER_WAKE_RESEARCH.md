# Receiver-side sleep wake: evidence and remaining gates

Updated: 2026-10-11. Normal Home input and reconnection after manual wake remain
separate from waking a sleeping console. No wake implementation is qualified.

## Current evidence after W1

The [Home wake advertisement W1](HOME_WAKE_W1.md) scan ran with the Switch
asleep. The remote was silent at baseline, then sent undirected, connectable
advertisements after each press. Their manufacturer data follows the Amazon
template above: company, product, `02 7d`, a two-byte key field and a counter.
Up produced a zero key field. Home produced a fixed nonzero key field and
stepped the counter once per press. The Switch stayed asleep throughout.

This meets the normal-use Home advertisement gate for this remote on one
session. It does not establish anything on the Switch side: free filter
capacity, filter survival through Horizon's sleep path, or `BtWakeAp`
assertion on a match.

## Evidence after passive D2 and S1

The source-owned [passive sleep/resume S1 diagnostic](SLEEP_RESUME_S1.md)
completed its one manual physical sleep/wake cycle. The fixed report and its
completion marker were fully read back and decode to a successful host-visible
PSC sleep/resume sequence with zero capture drops. The active companion and
unchanged native package both matched their pinned hashes. No post-resume IPC or
BLE records were captured. The opt-in has been recoverably archived, so S1 will
not run again on later boots.

This establishes a bounded observed PSC sequence around manual sleep and manual
wake, not controller filter ownership or retention, Home-specific advertising,
receiver-to-host signaling, deep-sleep wake, or Amazon-remote wake. The earlier
pre-suspend logging correction below remains part of S1, not a sufficient
sleep-retention test by itself.

The physical D2 capture passed for one observed boot. The checksum-valid,
build-matched retained controller response reports filtering support, a maximum
of **16 filters**, and **1,024 bytes of scan storage**. Full readback matched the
three deployed files. This is not attestation of every running byte, available
free filter slots, filter programming, suspend retention, or a wake interrupt.
Six extension bytes were not retained as raw data and remain uninterpreted.
See [the D2 record](CONTROLLER_PASSIVE_D2.md). No D2 repeat test is needed.

### Filtering is not yet a wake contract

| Source | Useful contract | Limitation on this Switch |
| --- | --- | --- |
| [AOSP HCI requirements](https://source.android.com/docs/core/connect/bluetooth/hci_requirements) | Capacity and advertisement-filter command definitions | Maximum capacity is not free capacity or a wake feature bit |
| [Infineon WakeOnLE](https://github.com/Infineon/linux-example-btstack-wakeonle) | Receiver filtering, scanning, sleep configuration and host/device wake signaling | CYW4356 is not listed; the standalone application clears the filter table and relies on a prebuilt experimental library |
| [Amazon MediaTek receiver source](https://github.com/Amazon-OSS-Kernels/android_kernel_common-android13_gki_aarch64/blob/308bb911d12ac01ce29fb7c98d42a1c705cba172/bootable/bootloader/uboot-mtk/mt9026/u-boot-2019.04/Customization/Mediatek/bluetooth/src/LD_btmtk_usb.c) | Amazon manufacturer filtering and receiver WoBLE setup | Chip-specific reserved indexes and commands are not a portable Broadcom wake implementation |
| [Linux Bluetooth suspend source](https://github.com/torvalds/linux/blob/master/net/bluetooth/hci_sync.c) | Suspend policy explicitly separates ordinary monitoring from wake scanning | Linux's accept-list, filter and resume policy is not evidence of Horizon's policy |
| [Public MissionControl](https://github.com/ndeadly/MissionControl) | Existing event forwarding and service integration | Public documentation excludes non-Switch-controller wake and BLE support; no drop-in wake path was identified |

Do not copy a standalone global filter-table clear into a shared Horizon
controller. Do not copy MediaTek's reserved slot choices into the captured
16-slot range. The broad Amazon company pattern is neither authenticated nor
Home-specific. No external implementation code was imported in this review.

### Source-owned logging correction, not deployed

The portable header recorder already accepted scan-result event6 and
filter-completion event7. Two enclosing runtime gates admitted only event6,
silently preventing the startup logger from recording event7. Both gates now
accept those two event types. Address comparison is restricted to event6;
event7 copies only its eight-byte result/action prefix, never an address.

The correction leaves the default-disabled opt-in, fixed 120-second window,
bounded queue, original event forwarding and power teardown unchanged. The
full sanitizer-enabled host suite passes. New tests compile the actual runtime
with fixture SDK plumbing and cover guarded prefix reads, all 65,536 selectors,
disabled/expired modes and queue saturation. Four translation units also compile
against the retained Switch SDK. These are source tests and object compilation,
not linking, deployment or device evidence for the changed logger.

The SDK's filter-completion record has result and action only, not filter index,
caller identity or request sequence. A completion is therefore not ownership
proof. Both the startup logger and the lease-scoped logger stop at suspend.
Neither can establish full filter lifetime across sleep. Extending observation
must first specify bounded in-memory capture, power ordering, post-resume-only
storage and failure/drop reporting without changing radio behavior. An absence
of callbacks while asleep would still not prove the absence of controller work.

Normal-use Home advertisement evidence is now met by W1. The remaining gates
are a shared-controller filter ownership/lifetime contract and demonstrated
receiver-to-host wake signaling. The pre-suspend correction alone was not a
physical test. The later S1 observer completed its bounded manual cycle and
remains a non-wake test.

## Historical research through 2026-10-09

The sections below retain the earlier evidence and its limitations. Earlier
statements that no capability reply was available are superseded by D2 above.
Descriptions of historical native replay do not describe work performed in
the October10 review, which did not execute native firmware.

## Historical findings

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Public Fire TV source has an Amazon advertisement filter and WoBLE setup | Receiver-side filtering is a real implementation direction | The exact K7Q3M7 Home sequence or compatibility with the Switch |
| Switch bus documentation identifies `BtWakeAp` on GPIO H5 | A Bluetooth-to-host wake signal exists | Which received events are allowed to assert it during Horizon sleep |
| Stored Horizon Bluetooth driver has advertisement-filter and sleep-mode command builders | Relevant command-building code is present in the selected image | Controller acceptance, live-image identity, or execution during suspend |
| Stored BTM sleep-path replay stops an owned active BLE scan, requests tracked GATT disconnections and selects LLR scan before acknowledging sleep | The selected host sleep path has deliberate scan/connection teardown | The live branch, radio response, complete filter lifetime, or an Amazon-compatible wake mode |
| Lower driver replay prepares controller-resident Classic link keys, requests a native vendor mode, then changes `ApWakeBt` output | This selected sleep path is state-changing and is not a generic read-only BLE wake probe | The vendor mode's on-air behavior, controller acceptance or Amazon BLE compatibility |
| Earlier passive remote advertisements match the broad Amazon manufacturer pattern | The observed candidate is consistent with that pattern | Home-only wake behavior; those captures were in pairing mode |
| Physical H1 read returned HID Information `00 01 00 00` | Standard `RemoteWake` and `NormallyConnectable` flags are both clear | Absence of every vendor-specific wake mechanism |

### Amazon receiver implementation

The public [Fire TV source mirror's MediaTek Bluetooth bootloader code](https://github.com/Amazon-OSS-Kernels/android_kernel_common-android13_gki_aarch64/blob/308bb911d12ac01ce29fb7c98d42a1c705cba172/bootable/bootloader/uboot-mtk/mt9026/u-boot-2019.04/Customization/Mediatek/bluetooth/src/LD_btmtk_usb.c#L1186)
contains `btmtk_usb_set_apcf`, a manufacturer pattern explicitly labeled
Amazon, configurable wake-device filtering, and a WoBLE setup sequence.
Its broad template checks company ID `0x0171` and a following `02 7d` marker,
with product matching subject to configuration. Our earlier candidate
advertisements satisfy that broad template. This comparison is not a match
against a verified live Fire TV configuration.

This file targets a MediaTek smart-TV platform. It is not evidence that every
Fire Stick uses the same controller command sequence. A separate
[Fire TV Stick platform device tree](https://github.com/Amazon-OSS-Kernels/android_kernel_mediatek_mt8695/blob/f618ca35ff71723dc9c5cb00e91ce1f9e83ad9d5/kernel/mediatek/mt8695/4.4/arch/arm/boot/dts/mantis.dts#L174)
also describes a WoBLE interrupt. Neither source proves the power state or
Home-button traffic of the user's particular Fire TV and remote.

### Switch-side compatibility surface

[Switchbrew bus documentation](https://switchbrew.org/wiki/Bus_services)
identifies the host-to-radio `ApWakeBt` and radio-to-host `BtWakeAp` signals.
This is the relevant physical architecture, not an application running while
the main processor sleeps.

An offline, SHA256-pinned review of the already-extracted Horizon 22.5.0
Bluetooth driver found 3 direct builders of Broadcom `Write_Sleep_Mode`,
11 direct builders of the Android-style Advertisement Packet Content Filter
command, and 1 vendor-capabilities request. The sleep commands use the public
12-byte parameter size. A read-only verifier checks exact decoded instructions,
nearby dispatcher calls and complete linear text coverage. These are static
code-presence results, not a capability response from the radio. Other indirect
command-building paths are not exhaustively enumerated.

Public interpretations are grounded in
[Broadcom's Android low-power implementation](https://android.googlesource.com/platform/hardware/broadcom/libbt/+/master/src/hardware.c),
[AOSP command definitions](https://android.googlesource.com/platform/system/bt/+/android-9.0.0_r7/stack/include/hcidefs.h),
and [AOSP HCI requirements](https://source.android.com/docs/core/connect/bluetooth/hci_requirements).
Controller capabilities include filtering support and filter capacity. Having
builders in the host binary does not establish either returned value.

The earlier read-only I1 capture reported matching 22.5.0 driver build metadata;
its retained binary checksum and receipt hash were reverified during this
research. That is historical loader metadata, not current runtime-byte
integrity, the active backend or Bluetooth-controller firmware identity. It
does not need to be repeated merely to obtain the same metadata.

[Infineon's WakeOnLE example](https://github.com/Infineon/linux-example-btstack-wakeonle)
demonstrates advertisement filtering with host/device wake signaling. CYW4356
is not in its supported-chip list. Some experimental controller operations
are supplied by a prebuilt library, so the application source is not a complete
portable command implementation. No code from that example was imported.

### Selected Horizon suspend path

A second offline verifier checks the already-extracted BTM image from the
22.5.0 bundle. It pins the image hash, checks 58 decoded instructions and five
driver-proxy slots, then replays the actual ARM64 control flow in a bounded
emulator. No firmware bytes are included in this repository.

For **BTM internal state 2 receiving PSC `SleepReady`**, the modeled successful
path is:

1. If BTM's own scan-active flag is set, request `StopBleScan`, clear that flag
   after a successful request, and wait for the completion event.
2. Run other preparation helpers, disable TBFC scan, make inquiry/page scan
   non-visible, and wait for pending connections to clear.
3. Enumerate tracked GATT connections. Request `DisconnectGattServer` for each
   non-invalid handle and wait for the closed event.
4. Request `EnableLlrScan`, change the internal BTM state, and acknowledge
   PSC `SleepReady`.

The two numeric state namespaces are different. PSC names come from the
[documented power-state interface](https://switchbrew.org/wiki/PSC_services#PmState).
Driver method identities are cross-checked against their IPC command numbers
and [public interface documentation](https://switchbrew.org/wiki/Bluetooth_Driver_services).
`EnableLlrScan`, `EnableTbfcScan`, `EnableRadio` and `StartBleScan` are separate
operations. These names do not by themselves establish the radio's mode,
accepted wake packets or low-power current.

The lower driver trace adds another constraint. `EnableLlrScan` selects a
backend through a different owner field than the normal bond API. One backend
returns success without sending a command. The alternate backend builds a BSA
extension request from selected per-device records. The verifier checks 37
instructions and four factory/vtable pointers for this route. It does not read
live device records or keys or identify the active backend. A further lower
driver replay is described below. There is no demonstrated path from adding
an Amazon BLE bond to inclusion in this sleep-time request. An API success
alone would therefore be inadequate evidence of receiver wake configuration.

Nine replay cases cover active/inactive scan state, zero/two tracked GATT
connections, an invalid handle, delayed scan-stop completion, and four failure
boundaries. Failure cases stop before sleep acknowledgement in the selected
non-recovery-policy path. Changed and truncated input images are refused.

The dependencies are deliberately synthetic: PSC requests, driver IPC results,
event payloads, timers, locks and inventories. Seven other preparation helpers
are stubbed and listed in the private receipt. Therefore this is a verified
**selected host control-flow path**, not a simulation of the controller, an
exhaustive sleep-path audit or a measurement on the running Switch. It does not
establish what happens to untracked connections or scans started outside BTM,
and does not establish whether filters survive the full suspend sequence.

This narrows the design: retaining our plugin's service session or leaving a
normal BLE scan active is not sufficient evidence of wake readiness. Horizon
has its own teardown policy. The receiver-offload route needs a controller-side
wake mechanism that coexists with that policy; the alternative is a supported
connection-preserving sleep mode, which has not been established here. Neither
route is achieved by saving the bond again or changing the Home HID mapping.

### Lower driver: controller preparation is not a wake capability probe

A third private, hash-pinned verifier follows the selected BSA sleep request
into its command builders and callbacks. It checks 55 instructions and two
pointers, then runs 12 bounded sleep-setup cases with invented device records
and four capability-completion cases.
The selected successful request chain is:

1. Request Broadcom sleep-mode configuration and await a synthetic successful
   completion in the replay.
2. Request deletion of all controller-resident **Classic Bluetooth** stored
   link keys, then write the selected records if the count is nonzero.
3. After the write's successful completion, or immediately for zero records,
   request a native vendor mode. Its controller-side semantics remain unproven.
4. On that mode's successful completion, request `ApWakeBt` output value 1
   when the driver's cached value differs, then issue the terminal event.

The standard stored-key command identities are cross-checked against
[BlueZ's HCI definitions](https://github.com/bluez/bluez/blob/master/lib/bluetooth/hci.h).
This is a request to alter the radio's Classic key table, not evidence that
Horizon deletes its persistent pairings. No actual keys were read and no
commands were sent. Even the zero-record case requests deletion. Consequently,
calling `EnableLlrScan` merely to ask whether wake is supported is unsafe: it
is state-changing, and substituting a BLE bond into Classic key records is not
a demonstrated interoperability mechanism.

The GPIO wrapper resolves to `SetDirectionOutput` (IPC 19), not
`SetValueForSleepState`. Its pad is `ApWakeBt`, the host-to-radio signal, not
`BtWakeAp`, the radio-to-host wake signal. These identities are independently
documented in the [GPIO interface](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/gpio/sf/gpio_sf_i_pad_session.hpp)
and [pad mapping](https://switchbrew.org/wiki/Bus_services#GpioPadName).
Output value 1 alone does not establish wake-event polarity or what radio
traffic can trigger a host resume.

The replay covers zero/one/two records, already-cached GPIO state and selected
queue, allocation and completion failures. It also confirms that the selected
callback ignores the delete-request return: a synthetic allocation failure
there still allows later steps to run. Thus even a terminal host success does
not independently certify every controller operation.

Admission checks and backend selection are statically traced, not replayed.
All eight native link-cleanup contexts are empty fixtures. Allocation, command
transport, completion scheduling and GPIO service results are synthetic; the
radio and sleep hardware are not simulated.

The capability-completion replay now follows the selected common dispatcher
to the query's no-op callback. In all four synthetic cases the response pointer
reaches that callback, but this chain does not read the capability payload or
write state outside the stack. This explains why that callback is not a source
of cached capability values. Earlier transport logging and other consumers are
not audited, so this does not establish that capability data are absent
everywhere in the stack. A captured response, not a host success code, remains
the relevant evidence gate.

The existing C2 logger cannot supply that response: its schema explicitly
accepts only LE connection-command status, not vendor capability replies.
No matching capability or Bluetooth-snoop capture was identified in the
scoped local artifact search. This is a missing observation, not evidence of
unsupported hardware, and no new logging firmware has been deployed.

### Observation tooling and acquisition boundary

`scripts/read_controller_capabilities.py` is an offline reader for existing
btsnoop v1 captures using HCI (1001) or H4/UART (1002) framing. It does not
capture traffic or talk to the console. Run it only against a private local
capture obtained through a separately reviewed collection path:

```sh
python3 scripts/read_controller_capabilities.py /path/to/private-capture.btsnoop
```

Only the command completions for Read Local Version and LE Get Vendor
Capabilities are decoded. The formats are grounded in
[BlueZ's btsnoop implementation](https://github.com/bluez/bluez/blob/master/src/shared/btsnoop.c),
[HCI structures](https://github.com/bluez/bluez/blob/master/lib/bluetooth/hci.h)
and [AOSP's capability prefix](https://source.android.com/docs/core/connect/bluetooth/hci_requirements).
No source implementation was copied. Unknown capability extensions are counted,
not guessed. Outgoing packets are only inspected for metadata-query and reset
headers; the tool sends nothing.

The report separates an absent reply, a failed query, explicit reported
filtering support and conflicting replies. It does not combine replies across
observed controller resets. Per-epoch query counts distinguish an observed
query with no reply from a capture with no query. Drops and truncated capture
records remain visible.
The file format cannot prove which physical controller supplied the data;
origin must be established independently. Reported maximum filter capacity is
not free capacity, active filtering, retention during sleep or wake support.

The parser skips unrelated packet bodies, including keys, advertisements and
HID traffic. It emits no raw packets, peer addresses, names or private paths.
Its tests cover 22 cases, including a guarded stream that refuses reads of an
unrelated event body, 1,000 bounded input mutations, reset separation and CLI
errors without partial output or private path disclosure. Raw captures must
remain private even when their filtered summaries are safe to share.

No suitable built-in capture path was established in this review. The public
driver interface and checked libnx headers did not expose a generic HCI logger.
[Published system settings](https://switchbrew.org/wiki/System_Settings#btp_config)
list logging options for `btp_config`, but their applicability to this selected
original-Switch driver was not established. Searching its stored data segments
did not locate the corresponding logging strings; that absence is not proof
that all logging is impossible. Those options were not enabled.

[nx-btred](https://github.com/plutooo/nx-btred) uses the audio driver interface,
not a generic HCI-capture interface.
[BlueKitchen's logger](https://github.com/bluekitchen/bluetooth-logger) supports
external trace hardware; it is not an installable Switch capture plugin. A
hardware diagnostic that power-cycles the radio would answer a different
question and was not run.

Before a temporary software observer is deployed, its integration must show:

- Passive receipt of the already-issued startup responses, with no replayed
  commands, resets, filter writes, pairing operations or wake-mode requests.
- An exact match to the active instrumented build, not only the older I1
  loader metadata. Original callback behavior and service ownership preserved.
- Bounded, nonblocking metadata capture with no file I/O inside the Bluetooth
  callback, no broad traffic dump and no secret-bearing response capture.
- Per-boot provenance, explicit missing/error outcomes, and verified rollback
  to the working installation before any operator-requested reboot test.

This reader is ready for offline inputs. The metadata-only observer core is now
implemented and tested, but its live acquisition transport is **not integrated
or deployed**, and the hardware capability response has not been obtained.

### Capture core and integration review

`mc_mitm/source/amazon_remote/controller_capabilities.hpp` contains a separate
receive-only decoder and bounded single-writer accumulator. It has no Switch
SDK dependency, radio interface, memory allocator, file operations or locks.
It retains decoded scalar fields from only the two selected replies. An error
reply reads only its status byte; an extended capability reply reads only the
defined prefix. Unknown extension bytes are counted, not retained. Malformed
responses do not inherit earlier success fields.

The accumulator distinguishes not-observed, controller-error, malformed and
conflicting outcomes, preserves the first response for comparison, separates
observed reset boundaries and admits at most 64 selected replies per instance.
Its per-boot tag is supplied by its owner and is only a correlation token, not
proof of controller identity. Reset notification records a boundary; it does
not reset the controller. Startup interception, trustworthy boundary delivery,
storage ownership and synchronization are requirements of the unfinished
integration. `ReadSerialized` is deliberately not a concurrent snapshot API.

Host tests with AddressSanitizer and UndefinedBehaviorSanitizer exercise all
65,536 opcode values, length/status/filtering boundaries, conflicts, resets and
the lifetime limit. Guard-page tests reject accidental reads of unrelated
payloads, error tails and unknown extension bytes. An independent local test
compiled the core for AArch64 and replayed 1,098 synthetic calls, including
cross-checks against the offline Python decoder. Those tested calls required
at most 81 instructions each, with no unresolved external symbols. Guarded
memory, stack restoration, callee-saved general registers and the ABI-preserved
SIMD halves were checked. These are compiled-core tests, not measurements of
callback overhead on the Switch or proof of a working driver hook.

Two existing integration shortcuts remain unsuitable without more work:

- The old event publisher uses the native queue, whose reviewed path checks
  readiness and enters a mutex-protected enqueue operation. It is not a proven
  nonblocking startup cache. MissionControl's event consumer waits for interface
  initialization before attaching that event. Early delivery must be established
  before relying on this path for startup replies.
- The inspected MissionControl implementation already exposes `SendHciCommand`,
  but its service waits for a custom reply without a timeout. It is an active
  command path, not a passive logger. A dedicated, bounded read-only query could
  be an alternative to startup interception; simply calling the existing
  generic interface does not meet the intended failure-handling contract.
  The later Q2 trial also exposed a native calling-convention mismatch in that
  installed legacy path. A caller-side timeout does not make it safe.

Neither shortcut was enabled during the offline phase. FTP subsequently became
available again, and current SD artifacts were checked against the retained
working build. That verifies installation files, not every live instruction.

### Q1/Q2 candidate withdrawn after native-service crash

The dedicated Q1 candidate passed host tests and was staged with rollback
copies, but its first trial failed a precondition. The isolated Q2 trial then
crashed the Bluetooth service before saving a result. The companion subsequently
aborted because its driver IPC session had closed.

Static review of the exact retained image and installed patch confirmed that
the legacy command route does not establish the native stack frame expected
by its return path. The specialized saved-peer route does establish that frame.
An earlier audit had already found this distinction; it was missed when Q1
reused the generic bridge. Fake-SDK tests and caller deadlines did not validate
or contain this native fault. Neither trial establishes a controller capability
response, command delivery, or wake compatibility.

Q1/Q2 are withdrawn. Their source is disabled, old staging scripts refuse
deployment, and recovery returns to the pre-query companion without changing
normal input, driver, pairing or boot configuration. See the
[failure analysis and withdrawal](CONTROLLER_CAPABILITIES_Q2.md).
Do not repeat either physical test. The next acquisition design must validate
the complete native transport, not just reply decoding and caller admission.

### Passive acquisition review, not a new console test

An offline P1 candidate now has a strict metadata event decoder, immutable
foreground diagnostic checkpoints, compiled ARM ABI/bounds tests and an
exact-artifact static queue/callback review. The selected stock request is
conditional; its callback's borrowed response must be copied immediately.
The matching cache/getter companion and read-only foreground reader now build
offline. A static initializer creates the native mailbox event and checks its
creation result, but complete callback/queue lock ordering and early delivery
before companion readiness remain unqualified. No native producer patch is
integrated and nothing from P1 has been deployed. Foreground checkpoints cannot
diagnose an earlier boot callback by themselves. See
[P1 scope and remaining gates](CONTROLLER_PASSIVE_P1.md).

The subsequent review found that merely reading events earlier would also
have to respect the companion's delayed driver-session initialization. The
preferred replacement now captures a single immutable metadata record without
using the startup queue, then retrieves it later. Its 36-byte capture core,
borrowed-envelope adapter and distinct 64-byte readout pass host tests and
133,080 guarded ARM calls in a separately linked source-owned image. The
loader/storage review rules out extending allocation through IPS alone, and
the existing input-only custom command cannot directly return the record.
The native binding, startup compatibility and retrieval transport remain
unimplemented. This is not a deployable diagnostic or wake implementation.

### Historical evidence gates, before D2

1. Establish controller identity and obtain its capability response through a
   reviewed observation path. Retain the prior I1 host-metadata evidence and
   check freshness when needed; repeating it alone cannot answer capability
   questions. Do not infer support from host code or C2 connection status.
2. Establish controller-side LLR/vendor-mode behavior, radio policy, filter
   ownership and host-wake handling beyond the verified host request chain.
   Establish which branch the running system takes. Do not invoke the native
   LLR setup as a capability probe. The public scan-filter IPC calls alone do
   not establish their lifetime during deep sleep.
3. Capture normal-use Home activity with an operator-confirmed button window.
   Do not hold Home into pairing mode or re-pair the remote to a different host.
   Compare Home with another button. CoreBluetooth may not expose directed
   advertisements, so a negative Mac scan is not a conclusive negative result.
   **Met on 2026-10-11 by [W1](HOME_WAKE_W1.md)**, with a positive result.
4. Only if the receiver capability and lifecycle are established, design an
   opt-in, reversible wake test that preserves native-controller wake and the
   committed remote bond. Keep false wakes and sleep current as acceptance
   criteria alongside successful Home wake.

Do not enable a broad Amazon-vendor wake filter as a shortcut. It could react
to other remotes, pairing traffic, or ordinary activity. Likewise, a matching
advertisement is not proof of an authenticated Home event. Filter ownership
must coexist with Horizon and other Bluetooth clients rather than clearing
their shared filter tables.

### Historical verdict, still not wake proof

A receiver-side implementation is plausible and has concrete supporting
evidence. It is not yet a proven software-only solution on the original
Switch, and no working wake build is claimed. An external bridge is a fallback
research direction, not a prerequisite established by these findings.
