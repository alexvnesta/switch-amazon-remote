# Connect timing P3

P2 rejected a target sighting that was 2977 ms old at physical ZL. Marker
persistence, logging and the existing ownership checks consumed approximately
36 ms, so the second 3000 ms freshness check rejected it before connection IPC.
This was not evidence of a remote pairing rejection.

P3 separates the physical decision from its short dispatch interval:

- Physical ZL still requires an exact configured target, valid advertising
  shape, HID/manufacturer corroboration and sighting age at most 3000 ms.
  It uses the real monotonic clock, not an older main-loop timestamp.
- That decision is valid for at most 250 ms, including log/file/IPC latency.
  Final dispatch must also precede the unchanged capture deadline.
- The final check revalidates the same snapshot against the accepted timestamp.
  Therefore the maximum allowed sighting age at dispatch is 3250 ms, not 3000 ms.
- User/system connection-list checks, exclusive durable marker, target
  fingerprint, input interpretation and D3 owned-link cleanup are unchanged.
  No automatic retry, pairing, bond deletion, scan/filter change or new client.
- A dispatch overrun still fails closed and retains the pending marker. Only a
  confirmed physical reboot permits the existing operator recovery workflow.
- Dispatch timing is logged after connection IPC, so logging cannot consume a
  budget that has already been checked. An accepted request is not link proof.

The adapter test replays the recorded boundary as a failing legacy negative
control and a passing P3 case. Additional tests cover 250/251 ms, logging
latency, stale/future/backward clocks, malformed/foreign sightings, expired
lease, retained markers, one-shot behavior, and fresh explicit resubmission
after a harmless stale sighting. The full adapter suite retains input and
cleanup coverage. Host tests do not establish console or bonding success.

P3 changes only the navigation module and its hash-bound Setup frontend. The
P2 observer stays byte-identical. Setup displays `CONNECT TIMING P3`.
