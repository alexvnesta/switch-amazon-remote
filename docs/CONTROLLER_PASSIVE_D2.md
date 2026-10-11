# Corrected passive observer D2

This is an experimental metadata observer, not deep-sleep wake support.
It retains the first complete controller-capability prefix or controller error
from an existing native callback. It issues no new controller/radio command.
Missing data means **NotObserved**, never Unsupported.

The supplemental image uses the inert legacy module prefix qualified by L2,
complete empty dynamic/hash/symbol metadata, a dedicated writable runtime-object
page, and a separate initialized endpoint page. Its allocation is four pages.
No imports, exports, runtime relocations, constructors or finalizers are added.

All initial hooks land inside the main image. Three source-owned guards perform
one fixed-page QueryMemory call only for the selected capture/read/event route.
They require an RX page at the expected addon address, an exact page length,
the canonical entry prefix and a distinct D2 ABI marker. They preserve arguments,
platform/callee registers and LR, and retain their route targets on the stack
across the syscall. A loader argument gap or inert L2 addon is rejected before
entering supplemental code. The marker is an ABI check, not runtime attestation.
Exact container and module hashes are separate deployment/reader gates.

The retired generic-command portion of the earlier bridge is reused. Neighboring
working Bluetooth hooks remain intact. Ordinary controller routes bypass the
query entirely. The old generic zero-address command path stays refused; the
existing saved-peer specialized routes are preserved.

The foreground report app verifies exact firmware/module identities and layout,
then requests one fixed stored-metadata event from the reviewed late-getter
companion. It writes immutable checkpoints before requesting it. Native queue
and mutex calls have no proven hard latency bound or IPC cancellation.

Synthetic tests execute only source-owned capture/adapters/guards. Native tails,
queues, locks and the kernel query are fixtures. Static native review is not an
exhaustive indirect-control-flow or startup proof. Private native firmware-containing packages
must never be published in this repository.

One controlled hardware boot and passive read passed on 2026-10-10. The four
immutable checkpoints, identity marker, snapshot checksum, run/context/manifest
bindings and finished summary validated with the build-pinned source validators.
Full readback of all three deployed files matched their reviewed hashes. Nine
local corrupted-copy or identity-mismatch controls were rejected. This is one
successful observation, not whole-running-byte attestation or reliability proof.

The retained 15-byte controller reply reported status zero, filtering enabled,
16 filter slots and 1,024 bytes of scan storage. Six extension bytes were not
interpreted. These are capability metadata, not proof of programmed filters,
suspend integration, BLE wake support or remote reconnection. The one-millisecond
stored-record read is not remote connection latency. No sleep or pairing test
was performed by the observer.

Activation changes only the reviewed native and companion NSPs and adds the D2
report app. Exact previous files and partial staging are preserved for no-delete
recovery. NAND, saves, bonding material, network blocking, firmware, Hekate,
Trinket, clock/voltage profiles and existing input module are outside its scope.
