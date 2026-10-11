# Lease-scoped passive scan diagnostic v2

This diagnostic does not fix discovery or claim native bonding. The filter-v1
user test accepted a corrected add but captured no device advertisements; its
saved result lacked a matching corrected delete despite own scan-stop API0.
Startup-only tracing expired before that test began, so it cannot resolve this.

`scan-diagnostic-mode.txt` must contain exactly `lease-scan-v2` and a newline.
The existing explicit, one-shot60s test lease remains the only capture gate.
Nothing starts a scan automatically. Late startup does not consume this window.
Suspend disables capture/storage permanently for the boot, without filesystem
work in the acknowledgement path. There is no retry, reset or extra event read.

This reuses the existing fixed SPSC intent/header queues, separately produced
by the existing IPC/BLE threads and drained only by Main. Requests47–49 and55–64
from HID/BTM are copied before original SF forwarding or the unchanged correction.
The command57/58 input is the ORIGINAL filter condition, not the corrected output.
Its actual correction results remain in the unchanged72-byte filter-v1 record.
The original radio calls, SF sentinel and real-driver service are unchanged.

ScanResult callbacks retain only the seven bytes BEFORE any peer address.
ScanFilter callbacks retain only the eight result/action bytes. The SDK labels
actions0/1/2/3/4 as add/delete/clear/enable/disable. Raw values and short input are
preserved without treating them as a correlated command completion or identity.
No peer addresses, names, advertisement payloads or security keys are copied
from callbacks. Filter/UUID request parameters are retained as diagnostic input.

At explicit End, Main exclusively creates `scan-diagnostic-v2.txt` once. It is
capped at32KiB with a reserved footer and separate queue/text overflow counts.
Existing or partial files are never overwritten, removed or retried. The final
snapshot is approximate: in-flight/late callbacks may fall outside the lease.
An empty trace, success callback or request is not RF, pairing or cleanup proof.

The source formatter now labels `0000000000ff00` as `stored_start_shape`.
A hash-bound review of the already-extracted22.5 driver found its start worker
writing little-endian FF00 at offset4, while its peer callback writes status2
at offset4. Both are forwarded as managed event6 without field rearrangement.
This is a diagnostic label, not proof of the current live build or an identified
remote. The SDK values and raw prefix remain unchanged. It does not shift the
peer parser, relax identity checks, enable filters, start scanning or fix ordinary
reconnection. `new_device_header` likewise does not prove address or identity.
Formatter writes remain bounded to448 bytes, within the existing512-byte scratch.
This formatter refinement has not been deployed or hardware-tested.

No manufacturer filter semantics, connection policy, pairing, controller
translation, native trial, Setup UI, boot flag or module IPC surface changes.
Device verification and a reviewed rollback deployment are still required.
