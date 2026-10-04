# Amazon remote background module: 0.2.0 candidate

This is a local-built, **inactive, device-untested candidate**, not an installed
remote driver. It isolates the device-proven probe0.1.10 transport from the new
background/controller layer. No private MissionControl BLE source is used.

## What is actually known

The K7Q3M7 test on Horizon22.5.0 produced109 decoded reports from two validated
input subscriptions, balanced34 button presses/releases across12 named buttons,
and over44seconds of connected observation after INPUT_READY. That was a
foreground logging-only probe. It does not prove durable bonding, background
ARUID0 access, virtual-controller output, suspend/resume, wake or microphone audio.

This candidate builds a libnx sysmodule plus a small operator-control NRO.
Portable tests cover65,536 button masks, staged HDLS resource failures, exact
configuration parsing, stale/replayed commands, session/suspend policy, continuous
hold safety, and exact-size complete-record file publication including Horizon's
non-overwriting rename semantics. The frozen actual transport
also runs through the original29 fake-SDK scenarios. Those are local checks, not
hardware validation.

## Architecture and scope

- `mc_mitm/source/amazon_remote/`: one shared reviewed transport implementation
  used by probe, module and public observer integration. No duplicated runtime
  snapshot is maintained in this public repository.
- `controller.hpp`: pure button mapping, continuous-hold guard, single-owned-pad
  resource lifecycle with sticky cleanup failures; no global controller-list APIs.
- `hdls_backend.hpp`: public libnx HDLS adapter, one aligned4KiB transfer buffer,
  neutral sticks, virtual Pro-controller convention from public sys-con. Other
  HDLS clients may still conflict at the service level; coexistence needs testing.
- `protocol.hpp`, `config.hpp`, `io.hpp`: typed bounded file commands/status,
  strict target parsing, one-session policy and power-state classification.
- `main.cpp`: AppletType_None, explicit FS/SM initialization, PSC power handling,
  passive MissionControl65000–65003 bridge, verified transport and owned output.
- `control/main.cpp`: A arms, ZL requests one fresh-target link, X stops, + leaves
  the UI. Exiting the UI deliberately leaves the bounded background test running.
  It does not access Bluetooth or inject input itself.

The existing passive observerv5 remains required to see the remote's HID16
advertisement. The candidate neither changes MissionControl nor uses its private
experimental binary. It does not remove filters, consume managed-driver events,
adopt a pre-existing target connection, clear the durable link marker, or perform
a MAC-only disconnect. UUID-path isolation from other custom BLE clients remains
unproven; do not run probe and module transport simultaneously.

Commands carry the current module-instance tick, increasing sequence and a2second
expiry. Link requests are never queued until a future advertisement. A stops being
available after one attempt or any suspend/stop/error. Boot is idle, never pairing.
The portable command token is an anti-stale/replay guard, **not authentication**
against another app with SD access. Logs are bounded256KiB plus two backups.
Horizon rename does not overwrite an existing file. Publication persists a
complete temporary record, removes only the previous regular destination, then
renames. The brief missing-record window fails closed; crash-atomic replacement
is not claimed. SD sharing/contention and file-publication latency must be tested.
A leftover temp file or ambiguous storage failure stops rather than clearing it.
Persistent logging must initialize successfully; a logging failure during active
transport stops the diagnostic rather than silently continuing without evidence.

## Default mapping

| Remote | Virtual Switch button |
| --- | --- |
| D-pad | D-pad |
| Confirm | A |
| Back | B |
| Home | HOME |
| Menu | + |
| Play/Pause, Rewind, Fast-forward, Voice, app keys | Unmapped |

No fabricated volume/system/media command, voice audio or remote-specific vendor
write is added. Home while awake might open HOME or its held-button quick menu;
that needs device evidence. This does **not** imply wake from actual sleep.

## Safety limits and permissions

This first background test deliberately retains one explicit capture/link per
boot,60seconds for capture and90seconds after target connection. A continuous
mapped hold of10seconds stops and neutralizes/detaches the owned pad. Every
error/disconnect/stop/suspend attempts neutralize, detach, release the transfer
buffer and close its service. Cleanup IPC failures remain visible, not relabeled
successful releases. All synchronous IPC lacks a guaranteed wall-time timeout;
power ordering remains a device test, not a locally proven safety property.

Firmware is restricted to22.5.0, where the descriptor/property observations were
made. The `bt`/`btm:u` wrappers internally supply ARUID0 in AppletType_None. That
server permission/ownership behavior has not been tested. A service denial stops
without retry, ARUID spoofing or a global `btdrvInitialize` fallback.

Separate program ID: `0100000000a4d001`; proposed custom PSC ID: `0xbe`.
Collision-check installed modules before activation. PSC dependencies are FS,
HID and BTM to request cleanup before those services suspend. PSC ordering and
registration compatibility remain unverified on this device. Awake/ReadyAwaken
restores status polling only; **no automatic reconnect** follows resume.

The NPDM has a finite service allowlist, no hosted services and only the SD-card
filesystem permission (bit21), not NAND/save/debug filesystem privileges. It
includes explicit basic IPC/event/transfer-memory SVC capabilities. A64 warnings
are errors. Service ACL and capability sufficiency still need actual launch proof.

The package intentionally contains **neither `flags/boot2.flag` nor `enable.txt`**.
Copying its module folder alone will not auto-start it, and even a manually
started module exits without the exact deliberate enable line. Do not activate
unattended. Preserve all boot, network-blocking, existing module and pairing state.

## Build and local verification

From the repository root with a configured devkitPro SDK:

```sh
make switch-build
make host-test
python3 scripts/check_public_tree.py
```

The private development workspace additionally checked historical working binary
hashes and generated NPDM/PFS0 permissions. Those raw receipts and personal paths
are not published. Public checks exercise transport, controller and protocol
behavior with sanitizers. None connects to or changes a Switch. `exefs.nsp` is an
ExeFS module container, **not a game NSP to install using DBI**. The SDK is not
bundled; see BUILD.md for dependency pins and observer integration.

Follow [TEST_PLAN.md](TEST_PLAN.md) during a supervised test. Background output is the next proof
boundary; don't implement persistent connection retry around an unproven one.

## Public references and reuse

- [switchbrew sysmodule template](https://github.com/switchbrew/switch-examples/tree/master/templates/sysmodule)
  supplied the AppletType_None/heap/FS pattern; this main is independently written.
- [public sys-con HDLS implementation](https://github.com/cathery/sys-con/blob/master/source/ControllerSwitch/SwitchHDLHandler.cpp)
  demonstrates public virtual-controller APIs; no USB stack or source was copied.
- [public sys-con power lifecycle](https://github.com/cathery/sys-con/blob/master/source/Sysmodule/source/psc_module.cpp)
  acknowledges Awake/ReadyAwaken and cleanup on ReadySleep/ReadyShutdown.
- [libnx HID debug APIs](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/hiddbg.h)
  and PSC APIs are checked against the pinned local source, not guessed wrappers.
- [MissionControl FAQ](https://github.com/ndeadly/MissionControl#frequently-asked-questions)
  warns that non-Switch controllers cannot wake in MissionControl. This supports
  caution, not a proof that a new BLE implementation can or cannot do true wake.

GPL-2.0-only for the new module/integration, MIT for the original decoder, ISC
for linked libnx. Preserve the accompanying license texts and source on sharing.
