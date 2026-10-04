# Switch Amazon Remote

Open-source work toward using Amazon Fire TV remotes with a homebrew-enabled
Nintendo Switch. **Early experimental developer project—not a finished driver.**

The K7Q3M7 remote has delivered real, decoded BLE button presses and releases in
our foreground diagnostic probe. We are developing a separate background
sysmodule that can translate them into a virtual Switch controller.

## Compatibility today

| Capability | Status |
| --- | --- |
| K7Q3M7 descriptor and device fingerprint | Verified on one physical remote |
| BLE keyboard/consumer notifications | Verified in foreground probe |
| Twelve named buttons, press and release | Verified; 109 reports, 34 balanced press/release edges |
| Background sysmodule | Builds; host-tested; **not yet device-tested** |
| HOME/menu navigation through virtual controller | Implemented candidate; **not yet device-tested** |
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
- `patches/missioncontrol-observer.patch`: our changes to **public** MissionControl
  for passive, target-only sightings and bounded capture-control IPC.
- `host_tests`: sanitizers, fake public-SDK tests and race/lifetime tests.

This does **not** contain or require unpublished MissionControl BLE source.
The public MissionControl observer integration is still required: unmodified
stock MissionControl does **not** provide the custom IPC used by this project.
This project is independent—not endorsed by Amazon, Nintendo or MissionControl.

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

We welcome careful hardware testing, public-API research, controller/lifecycle
improvements and support for additional remotes. Start with
[CONTRIBUTING.md](CONTRIBUTING.md). Do not relax identity gates or add blind
connection retries merely to make an unknown remote appear supported.

GPL-2.0-only for integration/module code; MIT for the original decoder. Public
libnx is ISC-licensed. See [NOTICE](NOTICE.md), [LICENSE](LICENSE) and `licenses/`.
