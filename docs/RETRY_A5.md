# Early-link recovery A5 preview

A4 sometimes received a target Connected callback followed 96 ms later by
Disconnected, result `0x1a`, reason `0x3e`. Its 500 ms list poll never adopted
that brief connection state. It then spent 30 seconds waiting and stopped.
This failure was before fingerprinting, encryption, subscriptions or input.

A5 adds a narrowly bounded retry **before a link is adopted**. It retains
A4 startup and A3 completed-session sleep recovery. It changes only the input
module and its UI. No companion, native patch, permissions or bond changes.

Recovery requires all of the following:

- Unchanged, non-overflowed target callback and HCI snapshot histories.
- Exactly one newly accepted target LE Create Connection command in the saved
  peer's capture window, preceding its Connected callback.
- Exactly two new target callbacks: Connected followed within one second by
  Disconnected with the observed `0x1a`/`0x3e` result/reason and identical
  client-interface and connection IDs.
- No adopted connection, fingerprint, encryption request, pending GATT work,
  subscriptions, scan or controller output.
- Both existing user and system connection lists report the target absent
  twice, at least 250 ms apart, and at least one second since the failure.
- Fewer than three requests, capacity for subsequent evidence, and at least
  30 seconds remaining in the existing 60-second capture window.

Before retry dispatch the module journals another attempt and repeats saved
peer status, both connection-list prechecks, callback-history validation and
deadline checks. It keeps the existing clients and saved peer preparation.
It does not cancel a guessed handle, reset native callback contexts, pair,
delete keys or alter radio filters. Only the normal public `btmuBleConnect`
request is repeated. The unchanged fingerprint/security/input checks must
complete on the eventual connection.

The public [libnx callback schema](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/btdrv.h)
defines Connected=0, Disconnected=2 and a 16-bit disconnection reason. Target
SDK static assertions verify the exact offsets used by our existing passive
snapshot. Matching time, target and callback tuple is not a native request
token or authenticated ownership proof. This preview still assumes no other
client is concurrently connecting this configured remote. Ambiguous evidence
stops recovery, and these callbacks never authorize disconnect or input.

Missing callbacks, bare timeouts, unsupported failures, security errors and
Stop remain terminal. Sleep during initial connection is not a retry trigger.
The physical reason for the original brief connection failure is unresolved;
this change addresses recovery, not an asserted radio root cause.

Host replay tests cover the recorded failure, successful connection, repeated
failures, limits, stale/changed histories, mismatched tuples, HCI errors,
missing receipts, late dispatch, presence, clock regressions and older-session
history. The actual `LibnxSession` implementation is also exercised against
synthetic SDK calls through failure, retry, fingerprint and saved-key encryption;
refusal, Stop, timeout, racing callbacks, exhaustion and cleanup are covered.
Hardware reliability and sleep/wake still require physical testing.
