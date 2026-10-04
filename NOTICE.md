# Attribution and licenses

The Bluetooth transport, bounded HOGP client, passive observer integration,
diagnostic probe, background-module candidate and tests were implemented using
public source/API contracts. No unpublished MissionControl BLE implementation
or proprietary firmware/game assets are included.

- Integration/module/HOGP code: GPL-2.0-only; see `LICENSE`.
- Original `remote_decoder.hpp/.cpp`: MIT; retain the adjacent decoder license.
- Public MissionControl integration patch: public base copyright notices remain
  intact; upstream by ndeadly and contributors, GPL-2.0.
- Linked libnx: ISC, switchbrew/libnx authors; see `licenses/libnx-ISC.txt`.
- switchbrew sysmodule template and sys-con public HDLS/power code informed API
  usage. The new module does not copy sys-con's USB/controller stack.

Amazon, Fire TV, Nintendo Switch, Atmosphere, devkitPro and MissionControl names
belong to their respective owners. This is an independent community project.

The captured HID descriptor is protocol metadata used for an exact-device gate;
it contains no account, serial, pairing secret, microphone recording or game data.
