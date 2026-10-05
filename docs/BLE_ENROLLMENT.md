# BLE enrollment and native registration: separate seams

## What the current code does

The proven session connects the configured target, validates VID0171/PID0427
and the exact149-byte report map, requests a NoMitm GATT read, subscribes to the
two verified standard input reports, and publishes through its owned HDLS pad.
The proven Navigation14 never calls `btmuBlePairDevice` or `btmBlePairDevice`. Successful GATT reads
and generic pairing events are not proof of a persisted bond.

The initial getter-only check queries system parameter IDs1/FFFF. Public
[BTM documentation](https://switchbrew.org/wiki/BTM_services#GetBleScanParameterGeneral)
identifies both as Nintendo company0553 profiles; ID1 is used for Palma. Empty
lists in these scopes are not an Amazon enrollment diagnosis. The earlier
negative result must not be used to justify rewriting or clearing bonds.

## Consolidated read-only audit

`tools/amazon-remote-pairing-audit` opens/closes its own clients and calls exactly:

| Getter | Question answered | Not established |
| --- | --- | --- |
| `btmuBleGetPairedDevices` with company0171/pattern0427027d0000 | Is the configured address returned for an AD-derived Amazon query profile? | Profile suitability for enrollment, authenticated/durable bond or completeness of other scopes |
| `btmGetDeviceCondition(BtmProfile_Hid)` | Is the configured address in the current native HID device list? | Saved pairing or permanent HID registration |
| `btmsysIsGamepadPairingStarted` | Is native gamepad pairing currently active? | Remote enrollment, or the specific foreground page |

The Amazon parameter is copied from the first eight manufacturer AD bytes
captured for the configured K7Q3M7: `71010427027d0000`. It is a query hypothesis,
not a reverse-engineered manufacturer schema or a validated scan/pair policy.
Neither a negative query nor success of the IPC validates that policy.

The native HID condition structure contains no link key; do not substitute
`GetDeviceInfo` or the Settings paired-record API, which include key material.
HDLS output can work without the physical BLE remote appearing in BTM's classic
HID list. Such absence would not contradict the confirmed HOME navigation.

No scan/filter mutation, connection request, pairing request, events, keys,
controller output or pending-marker recovery occurs. Three independent clients
close even when a getter fails. The checksummed88-byte record contains API
results/counts, exact-target match counts, the query parameter and a pairing
flag, never addresses/names/device records. Saving is exclusive in its own
directory, without replacing earlier records or tools. Horizon22.5.0 is required.

Run once: open **Amazon Remote Pairing Audit**, press A once, press + to exit,
then open FTPD. No remote flashing, re-pairing or reboot is needed.

## Next implementation decision

1. Inspect the actual Amazon-profile query and native HID baseline. A matched
   entry is useful evidence of inclusion, not permission to skip persistence
   checks. A negative result does not establish a missing bond. The user-authorized
   Navigation15 trial explicitly labels the AD-derived parameter as experimental;
   do not guess additional scopes or reset other controller records.
2. Explicit BLE enrollment, if needed, belongs to the existing target-validated
   session after fingerprint/security-read checks and fresh connection/address
   revalidation. It needs a deliberate user enrollment action, one request with
   recorded Results, post-request same-scope inclusion evidence and a later
   ordinary-reboot check. A generic event must not be promoted to target success.
   This audit does NOT implement or issue that pairing request.
3. Native HID registration remains separate: MissionControl's public classic
   HID Opened callback enters a factory that aborts when classic paired-device
   information cannot be fetched. Neither a BLE connection nor BLE-list inclusion
   automatically enters that path. Do not fabricate a classic record/link key or
   inject fake lifecycle events to make the check pass. Reuse the upstream report
   classes through a tested registration seam; retain proven HDLS meanwhile.

The existing module permission list already includes `btm:sys`; reading its
native-pairing flag does not require adding privileges. That flag can eventually
serve as an enrollment-window signal, but it must be combined with explicit
opt-in and target checks. An active flag is not a reason to pair ambient devices.
No new menu detector, application-ID heuristic or custom controller protocol is
needed to test this public getter.

## Separate Navigation15 candidate

The getter-only audit remains unchanged. [Navigation15](NAVIGATION15.md) now adds
one explicit operator-Y request through the existing verified session, not through
the audit app or automatically through the native pairing-window getter. It uses
the public `btmuBlePairDevice` wrapper with the fresh configured-target handle and
the same experimental AD-derived profile. Its exclusive synced intent blocks
repeat requests across restart; post-query results distinguish request acceptance
from same-scope inclusion. Device enrollment and post-reboot persistence remain
unverified until those actual tests. Native HID registration is still separate.

References: [public libnx BLE user API](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/include/switch/services/btmu.h),
[native pairing getter](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/include/switch/services/btmsys.h),
[key-free device condition type](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/include/switch/services/btm_types.h),
[MissionControl lifecycle/factory reuse audit](MISSIONCONTROL_REUSE.md).
