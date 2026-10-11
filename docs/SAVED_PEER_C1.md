# Saved-peer readiness C1

Experimental, fixed K7Q3M7/public-address saved-bond path. Not a general remote
driver or proven cold reconnect. Local qualification is not device evidence.

The existing private `SAVED` record does not populate the Bluetooth driver's
volatile peer metadata. The reviewed native connection implementation selects
LE transport, but also refuses a peer whose resolved device type lacks BLE.
With no native peer or inquiry record, the default is classic-only. Successful
scan-assisted enrollment does not establish cold-start readiness.

C1 separates the steps:

1. An explicit Setup A opens the existing one-shot capture window.
2. The input owner requests preparation of the validated saved peer. Empty,
   malformed, wrong-owner, expired and repeated requests are refused.
3. The privileged companion submits one metadata-only request on the existing
   native serialized queue. Normal BLE registration is used only if that fixed
   peer is absent. A full cache fails without eviction. An existing record must
   already match the tested BLE/public identity and be idle; it is not rewritten.
4. Native metadata is resolved/read back. Inquiry metadata can affect this
   resolution, so this native helper is not described as strictly read-only.
5. Only confirmed readiness enables one physical ZL connection request. No
   advertisement is fabricated and no discovery filter is broadened. Queue or
   IPC acceptance does not enable ZL.
6. The existing owned-link fingerprint, stored-key encryption, confirmation,
   input and cleanup checks remain. No re-enrollment fallback is permitted.

`PREPARING BLUETOOTH` and `READY TO RECONNECT` are separate Setup states. Native
readiness is not remote presence, remote bond retention or encryption success.
Readiness expires with its capture window. C1 uses its own session directory,
`/config/amazon-remote-peer-c1`, leaving prior stopped B3 evidence untouched.

The native bridge remains exact-image and fixed-peer. Preparation carries no
keys, never requests a link or pairing, and retains one small queue context per
operation until reboot. Cancellation prevents subsequent application connection
admission; an already queued metadata operation may still finish. The existing
private-event consumer erases internal events before logging/public forwarding.

The C1 observer, native patch, input module and hash-pinned Setup must be reviewed
and deployed as a matched bundle. Do not install its native patch alone. Preserve
the prior bundle and all pairings, private saves, network blocking and boot state.

Local tests cover native metadata gating and normal registration, C1 native
admission, prior B1 encryption/relay behavior, the production companion runtime,
real transport readiness, stale/failure cases, and UI error precedence. All radio
and remote behavior remain hardware-test requirements.
