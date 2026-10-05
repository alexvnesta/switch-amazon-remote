# Atmosphere remote integration design

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

Navigation14 verified one manually enrolled K7Q3M7 session and actual HOME input.
Current code already runs input as a native background process; the companion
does not deliver input. It still has temporary candidate IDs, a diagnostic
pending marker and one-shot manual lifecycle. This is not yet a boot-ready
compatibility release or proof of durable bonding.

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
