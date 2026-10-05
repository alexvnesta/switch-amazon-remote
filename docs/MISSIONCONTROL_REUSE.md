# MissionControl reuse audit and local adapter

Audited public upstream: `71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e`.
The working installed observer and Navigation14 files remain untouched.

## What is reusable and what is missing

| Component | Public upstream evidence | Integration decision |
| --- | --- | --- |
| Button storage and Switch report formatting | `SwitchButtons`; `EmulatedSwitchController::FillBasicInputReport` | Reuse directly; do not duplicate the report protocol |
| Switch controller command replies and virtual SPI memory | `EmulatedSwitchController`, command processors | Reuse if native HID registration succeeds; not a BLE bonding implementation |
| Controller handler registration | HID Opened event calls `AttachHandler`; it requires classic paired-device information | BLE connection alone does not enter this path |
| Native pairing visibility | Classic inquiry/SSP names rewritten for allowed device classes; BTM device-info lists rewritten for HID profiles | Not an existing BLE enrollment hook |
| BLE events | Public BLE handler forwards managed events; no controller creation/subscription path | Keep independently built BLE/GATT transport; add only the missing bridge |
| Connection ownership/reconnect | Classic close/paired-info paths operate on classic HID devices | Need separate BLE ownership/bond evidence; do not use fake HID pairing keys as BLE keys |
| Diagnostic session budgets | Project one-shot test safeguards, not an upstream requirement | Not a production framework or prerequisite for native-menu work |

Source inspection identifies these seams; it does not prove that a synthetic HID
connection will register successfully with Horizon. Merely adding VID0171/PID0427
to the controller factory is insufficient: the upstream factory reads classic
paired-device information with an abort-on-failure path. Calling it on an
unregistered BLE remote would risk a module crash. Do not do that on the console.

## Implemented local reuse proof

`missioncontrol_buttons.hpp` maps the shared remote decoder into upstream's
actual `SwitchButtons` type. Those are Switch report bits, not libnx HDLS bits.
An opt-in host test pins the unchanged public header's SHA256 and tests65,536
masks plus independent keyboard/consumer holds and releases. Its tiny host shim
provides only primitive types/macros, not a replacement controller implementation.

`AmazonBleController` inherits `EmulatedSwitchController`, uses that mapper,
resets analog sticks, and delegates report encoding to the upstream formatter.
Encoding uses an aligned, initialized local report before copying to the caller.
It is deliberately NOT added to the factory or wired to any event/IPC handler.
The adapter itself has no Bluetooth call, report injection, scan/pairing action
or initialization of persistent virtual memory. A successful SDK build proves
source/link compatibility only, not a functioning native connection/report route.

This retains the proven HDLS path while testing the more integrated alternative.
No rewrites of Switch report commands, controller emulation or pairing UI are
justified until the registration seam is understood and can be tested safely.

## Read-only paired-list check

The standalone `tools/amazon-remote-bond-check` NRO requests existing system
scan-filter parameters for the two IDs documented by public libnx (`1`, `FFFF`),
then queries paired addresses using those returned parameters. It never invents
a vendor filter, starts/stops a scan, connects, pairs, unpairs, consumes events,
touches controller output or changes the pending marker. It opens/closes only its
own `btm:u` client. It does not read keys or persist any device addresses.

The report records API results/counts and exact configured-target match counts.
The built-in scopes are Nintendo-company accessory profiles, not an Amazon
profile. The [BLE enrollment audit](BLE_ENROLLMENT.md) corrects that diagnostic
limitation with an AD-derived Amazon query and key-free native-state getters.
A positive match means a target record was returned in that query. A negative
match is NOT proof of absence from all system bond storage: these are filtered
scopes. Neither result establishes encryption, authenticated identity or durable
bonding. Comparing positive records across a later ordinary reboot can provide
additional persistence evidence; there is no reason to reboot for this first
getter-only check. The NRO is restricted to the device-tested Horizon22.5.0.

To run once after reviewed staging: open **Amazon Remote Paired-list Check**,
press A once, then + to exit and reopen FTPD. No flashing remote/re-pair/reboot.
It creates one exclusive80-byte checksummed record in its own app directory.
Failure or partial saving is reported; it does not overwrite old records or retry.

## Local verification commands

```sh
python3 scripts/test_missioncontrol_reuse.py --source /path/to/pinned/MissionControl
make host-test
make -C tools/amazon-remote-bond-check
```

For SDK source compatibility, use the existing observer-patch preparation in
BUILD.md; copy the current `amazon_remote` source, then build the complete upstream
module. Keep this proof artifact INACTIVE; do not deploy it as a new observer.

Public source references:

- [Controller factory](https://github.com/ndeadly/MissionControl/blob/71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e/mc_mitm/source/controllers/controller_management.cpp)
- [HID lifecycle](https://github.com/ndeadly/MissionControl/blob/71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e/mc_mitm/source/bluetooth_mitm/bluetooth/bluetooth_hid.cpp)
- [Controller emulation](https://github.com/ndeadly/MissionControl/blob/71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e/mc_mitm/source/controllers/emulated_switch_controller.cpp)
- [Classic discovery handling](https://github.com/ndeadly/MissionControl/blob/71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e/mc_mitm/source/bluetooth_mitm/bluetooth/bluetooth_core.cpp)
- [Public libnx BTM user APIs](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/include/switch/services/btmu.h)
