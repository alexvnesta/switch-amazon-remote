# Read-only enrollment context, v3

Navigation15S made one system `btmBlePairDevice` request. BTM rejected it with
`0x35E8F` (module143, description431). The earlier user request returned
`0x5528F` (description681). Navigation continued; STOP detached and released the
owned pad successfully. Changing the service route did not establish a bond.
The public error descriptions reviewed do not establish the meaning of431.

The pinned libnx wrapper and public BTM description agree on the12-byte input:
eight bytes of advertisement parameter followed by a32-bit connection handle.
The profile is still an advertisement-derived hypothesis, not a validated
enrollment policy. Successful scoped paired-list getters do not validate it.
Public HID documentation shows `PairPalma` can invoke `BlePairDevice`, but does
not establish that a non-Nintendo remote can enroll with arbitrary vendor bytes.
Do not pretend this remote is Palma or guess Nintendo scopes for enrollment.

## One getter-only check

Open **Amazon Remote Enrollment Context**, press Joy-Con A once, then + to exit
and reopen FTPD. No reboot, remote flashing, reconnect or enrollment request.
Keep Navigation15S stopped; do not run old navigation/probe apps alongside it.

The app reuses the existing audit's clients, parameter and count validation:

- User: `btmuBleGetConnectionState`, `btmuBleGetPairedDevices`.
- System BTM: `btmBleGetConnectionState`, `btmBleGetPairedAddresses`.
- System pairing: `btmsysIsGamepadPairingStarted`.

Only result codes, counts, exact-target match counts, matching connection
handles and the pairing flag enter the exclusive128-byte checksummed record.
No address, name, device record, key or opaque condition-list bytes are saved.
Each successfully opened client closes once, even when its getter fails.
Connection buffers are bounded; a filled buffer, duplicate address/handle or
invalid handle is refused. The app saves once, syncs, closes and checks full
readback. Partial evidence is preserved and never overwritten/retried.

## What this can decide

Different target handles or visibility between the APIs would expose an
assumption needing investigation before another pairing trial. The same handle
would weaken that particular explanation, not prove connection ownership.
No target in either list does not prove radio absence; this app will not connect
to obtain a better-looking result. Lists are observed sequentially by a fresh
foreground client, not by the earlier module at the exact rejected call.
Comparison is indeterminate if either connection query fails or the collection
clock regresses/exceeds2s. Request acceptance, encryption, durable bonding and
native HID registration remain separate, unproved outcomes.

A false native-pairing flag is only a snapshot. It does not prove that flag must
be true for BLE enrollment. This app never starts native pairing or changes
filters/radio state. The proven module/observer and all intent/rollback files
remain unchanged. No new module permission, controller creation or fallback is
introduced. Supported firmware remains the device-tested Horizon22.5.0.

Sources: [system libnx contracts](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btm.c),
[user libnx contracts](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btmu.c),
[BTM wire description](https://switchbrew.org/wiki/BTM_services#BlePairDevice),
[Palma pairing description](https://switchbrew.org/wiki/HID_services#PairPalma),
[result-code format](https://switchbrew.org/wiki/Error_codes).
