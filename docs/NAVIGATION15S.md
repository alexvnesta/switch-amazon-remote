# Navigation15S: fixed background-BTM enrollment route

Navigation15 delivered user-confirmed HOME navigation, remained READY for about
6.4 minutes and stopped with successful virtual-pad detach/release. Its one
explicit user-BTM enrollment request failed with `0x5528F` (BTM module143,
description681). That code does not establish the precise failure cause; no
post-request listing or durable bond was proved. Late-idle input delivery was
not demonstrated by those logs.

This revision changes only the enrollment service route: system
`btmBlePairDevice` (cmd41) and system `btmBleGetPairedAddresses` (cmd44) through
the already-open BTM client. It never falls back to another API after failure.
The AD-derived0171/0427027d0000 profile is unchanged and still experimental.
This isolates the service-route question rather than changing route, profile
and security behavior together. Public source confirms the wire contracts, not
that this combination will enroll the Amazon remote on Horizon22.5.0.

Fingerprint/report-map/security-read checks, fresh target-handle checks,
neutral-input and list-capacity guards, exclusive synced/readback intent,
one request,10s follow-up query budget and bounded30min READY lifetime remain.
No driver initialization, ARUID borrowing, unpair, global radio/filter change,
fake controller record or lifecycle event is added. No additional privileges.

The temporary program ID remains `010000000000a4d9`; evidence and one-shot
intent files use a fresh `config/amazon-remote-nav15s` namespace. The previous
module binary must be preserved in a rollback archive before replacement. Its
old control app intentionally fails the binary pin once this revision is staged;
use only **Amazon Remote Navigation15S** after confirmed reboot/marker recovery.
Navigation14, observerv6 and all prior configs/intent/logs stay untouched.
Neither version gains a boot flag or automatic pairing/reconnection.

## One supervised test

1. Physically reboot, open FTPD and confirm before opening Navigation15S.
   Root recoverably archives the previous link marker after confirmation.
2. Open Navigation15S. A once, release ALL buttons1s to launch IDLE. Wait for
   IDLE health PASSED, then A to arm; flash the remote; ZL once at Link_READY1.
3. At READY/Pad1, release remote buttons and press Joy-Con Y once. Wait10s and
   note Enrollment/Result. Do not repeat A/ZL/Y, even after failure.
4. Exit+, test HOME navigation. If testing sustained use, wait3min with buttons
   released and then test again. Reopen this SAME app, X to stop, + to exit,
   then FTPD for log capture. Do not reboot before capture.

Result0 means request acceptance, LISTED means inclusion in the queried scope;
neither proves encryption, native HID registration or reboot persistence. A
post-reboot getter-only audit is appropriate only after the actual request and
listing evidence have been captured. No wake/microphone support is claimed.

Sources: [public system pairing/list wrappers](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btm.c),
[public user wrappers](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btmu.c),
[BTM wire descriptions](https://switchbrew.org/wiki/BTM_services#BlePairDevice).
