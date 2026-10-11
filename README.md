# Switch Amazon Remote

Open-source work toward using Amazon Fire TV remotes with a homebrew-enabled
Nintendo Switch. **Early experimental developer project—not a finished driver.**

The K7Q3M7 remote has delivered real, decoded BLE button presses and releases in
our foreground diagnostic probe. A later manually launched Navigation14
background sysmodule also delivered device-confirmed HOME navigation. This is
one successful session, not yet an everyday pairing/reconnect driver.

## Compatibility today

| Capability | Status |
| --- | --- |
| K7Q3M7 descriptor and device fingerprint | Verified on one physical remote |
| BLE keyboard/consumer notifications | Verified in foreground probe |
| Twelve named buttons, press and release | Verified; 109 reports, 34 balanced press/release edges |
| Background sysmodule | Navigation14 manually launched and delivered input on one console |
| HOME/menu navigation through virtual controller | Navigation14 verified D-pad, Select, Back, Home and Menu in one session |
| Sustained background use beyond90s | Navigation15 candidate built for30min Ready; device test pending |
| Pairing through native Controllers menu | Design goal; not implemented |
| Persistent bonding and automatic reconnect | Not established; no automatic retry |
| Actual sleep/resume or waking the Switch remotely | **Not supported/verified** |
| Microphone/voice audio | **Not supported**; Voice button event only |
| Other Fire TV remote models | Untested; exact-fingerprint gate rejects unknown models |

Device evidence is currently limited to a first-generation Switch on Horizon
22.5.0, Amazon K7Q3M7, VID `0171` / PID `0427`. The module explicitly rejects
other firmware versions until their public-SDK layouts and permissions are
validated. See [compatibility and evidence](docs/COMPATIBILITY.md).

## What's included

- `tools/amazon-remote-probe`: the 0.1.10 foreground input diagnostic.
- `tools/amazon-remote-module`: inactive 0.2.0 sysmodule candidate and arm/stop UI.
- `mc_mitm/source/amazon_remote`: public transport, bounded GATT client, decoder
  and passive observer source. Kept in this layout to make the observer patch
  reproducible against its public upstream.
- `patches/missioncontrol-capture-preflight-p2.patch`: apply after the existing
  observer, core-security, filter and lease-scan patches. Adds read-only capture
  availability/first-close-request metadata. Window Check P2 uses this before
  offering Start/Search. It does not extend, reset or rearm the capture lease.
  The first recorded request is evidence, not an attribution of the winning
  operation when close requests race. Pairing, radio and controller code are
  unchanged. P2 still needs console validation; no new wake/reconnect claim.
- `patches/missioncontrol-observer.patch`: our changes to **public** MissionControl
  for passive, target-only sightings and bounded capture-control IPC.
- `patches/missioncontrol-core-security.patch`: apply after the observer patch;
  adds a target-only core-event metadata tap and read-only getter. Off-device
  candidate only; PIN/SSP requests and raw link subtypes are not bond proof.
- `tools/amazon-remote-pairing-audit/driver_state_main.cpp`: one-shot read-only
  `btdrv` command100 getter, with an owned session and a metadata-only64-byte
  record. No driver initialization or shared-event reads. True can exclude the
  default stub only when the live image matches the reviewed stored firmware;
  false/errors do not identify a backend. Neither response establishes BLE
  bonding, encryption or persistence. Requires firmware22.5.0; no reboot or
  remote pairing mode needed. Decode with `scripts/read_driver_state_snapshot.py`.
- `tools/amazon-remote-pairing-audit/native_bond_owner_main.cpp`: the current
  experimental operator-triggered native command10 trial. The GUI sends one
  fresh, instance-bound command to Navigation17; it opens no Bluetooth clients.
  The owner uses its existing user/system/driver clients, with current target
  checks before and after durable intent. This avoids the extra `btm` client
  which returned `sm::OutOfSessions` in the standalone trial. Off-device tests
  pass; device bonding and persistence remain unverified. The retained
  `native_bond_main.cpp` is the old standalone diagnostic, not the current build.
  Requires the exact pinned Navigation17 runtime in neutral READY, its fresh
  same-session Y context, an exact current target/handle match, enabled driver,
  exclusive durable intent and revalidation before one request. No retries,
  reset, shared-event consumption, key reads or managed-UUID spoofing. Request
  acceptance is an unknown asynchronous outcome, not a bond or persistence
  claim. The transport field does not force BLE in the reviewed implementation.
- `tools/amazon-remote-pairing-audit/core_main.cpp`: read-only collector for
  retained observer-v7 metadata. It does not arm capture or initiate pairing.
- `host_tests`: sanitizers, fake public-SDK tests and race/lifetime tests.

This does **not** contain or require unpublished MissionControl BLE source.
The public MissionControl observer integration is still required: unmodified
stock MissionControl does **not** provide the custom IPC used by this project.
This project is independent—not endorsed by Amazon, Nintendo or MissionControl.

The intended end state is a native-feeling Atmosphere background module, with
one-time setup and reliable target-only reconnection—not an app kept open.
Alpha.2 adds a Bluetooth-free neutral-controller preflight and output/cleanup
telemetry. These are test tools, not finished controller support. See the
[native-experience plan](docs/NATIVE_EXPERIENCE.md) and its hardware proof gates.
The [plugin architecture](docs/PLUGIN_ARCHITECTURE.md) separates sustained input,
bond/ownership evidence, reconnect, native-menu enrollment and boot activation.
The [Navigation15 test](docs/NAVIGATION15.md) extends only the Ready lifetime;
it does not implement the remaining production features.

## Build and test

Portable checks need a C++20 compiler, Make and Python 3:

```sh
python3 scripts/check_public_tree.py
make host-test
```

Switch builds require an installed devkitPro SDK with devkitA64, libnx and its
tools. See [build instructions and pinned provenance](docs/BUILD.md).

```sh
export DEVKITPRO=/opt/devkitpro
make switch-build
```

Builds do **not** install or start anything. `exefs.nsp` is an ExeFS module
container, not a game NSP. No boot2 flag or enable configuration is supplied.
No prebuilt “stable support” release is advertised yet.

Experimental downloads are on [GitHub Releases](https://github.com/alexvnesta/switch-amazon-remote/releases).
The probe and **inactive** module are separate assets with source, checksums and
build provenance. They still require the custom observer; release builds are not
hardware-validation evidence. See [release policy](docs/RELEASING.md).

## Safety and testing

The probe and module use explicit operator arming, a fresh configured-target
advertisement and an exact descriptor fingerprint before notification setup.
They do not clear radio filters, initialize the global Bluetooth driver, blindly
pair nearby devices or adopt another client's existing connection. The first
module candidate retains a 90-second connected-input limit and a 10-second
continuous-button-hold watchdog.

Every attempted link keeps a persistent safety marker. **Do not delete it to
retry without a confirmed physical reboot and preserved diagnostics.** Running
multiple custom BLE clients together is not validated. Service replies and host
tests are not proof of bonding, navigation, cleanup or safe power behavior.

Read the [module design](docs/MODULE.md) and [supervised test plan](docs/TEST_PLAN.md)
before activation. Keep your known-working boot/recovery route and backups.
There are no game files, firmware images, NAND data or console keys in this repo.

## Contributing

The supervised `0.1.6-post1` Trial build is a **read-only post-request check**,
not another bonding attempt. It reuses the existing sequential user/system
connection and AD-scoped paired-list getters after validating an accepted owner
result, its durable intent and the same cleanly stopped module session. Only
physical Joy-Con A collects metadata; one exclusive `postcheck.bin` is synced
and read back. Existing or partial evidence blocks repetition. It never
publishes a module command, opens the native driver, reads keys or changes
pairings/radio state. The paired-list scope remains experimental; an empty list
does not disprove bonding, and a match does not prove encryption or persistence.
The request frontend is retained as source for regression checks, not built into
this Post1 NRO. This is not a production release or hardware-validation claim.

We welcome careful hardware testing, public-API research, controller/lifecycle
improvements and support for additional remotes. Start with
[CONTRIBUTING.md](CONTRIBUTING.md). Do not relax identity gates or add blind
connection retries merely to make an unknown remote appear supported.

GPL-2.0-only for integration/module code; MIT for the original decoder. Public
libnx is ISC-licensed. See [NOTICE](NOTICE.md), [LICENSE](LICENSE) and `licenses/`.
