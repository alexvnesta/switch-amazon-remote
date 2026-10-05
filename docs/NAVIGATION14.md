# Navigation14: targeted connection evidence

This is a supervised diagnostic candidate, not a released compatibility fix.
Navigation13 accepted a fresh target advertisement and a managed connect request,
but the connection list remained empty until its30s timeout. The operator reported
that the remote kept flashing and confirmed a subsequent physical reboot. The
controller-lifetime recovery was not reached in that attempt.

## Changes and limits

Navigation14 retains Navigation13's connection, GATT, controller lifetime,
release gate, status-publication and cleanup policies. It adds a read-only snapshot
from passive observer v6. The observer's existing BLE event tap copies the first
eight exact-configured-target client-connection callbacks during the existing
explicit60s capture lease, including nonzero results and unfamiliar status/reason
values. Overflow is explicit and records are never replaced. No MAC, bond/key data
or foreign callback is returned. Raw result/status/reason fields are diagnostic
evidence, not decoded radio-error claims, pairing proof or request ownership.

The old120s startup log and its success-filtering parser are unchanged apart from
the version banner. This separate capture does not rely on startup logging, SD
availability or the log's64KiB cap. It has no driver IPC, event read, allocation,
filesystem access, lock or wait in the producer. Snapshot readers make at most two
coherence attempts. The lease is still one-shot; it ends on expiry, explicit End
or suspend and is not extended for a connected session. An in-flight diagnostic
copy may finish after End; this cannot authorize an action.

Observer IPC65004 is read-only. Navigation14 checks that the new interface returns
a valid empty snapshot before Bluetooth initialization, then reads at most twice
per second and once during cleanup. It logs each new retained callback once.
Malformed, changed, future, overflow or inaccessible evidence is explicit; it is
never converted into success, a retry, connection adoption or controller input.
An access failure during an active test disables further diagnostic reads without
changing the connection algorithm. An empty snapshot means no matching callback
was captured by this tap, not proof that no radio attempt occurred.

The passive observer's current installed binary must be preserved before v6 is
staged. Its module ID, original boot flag, permission policy and classic-controller
handling must remain unchanged. Navigation14 has separate ID
`010000000000a4d8` and namespace `config/amazon-remote-nav14`, no boot flag, and a
launcher pinned to the exact staged module SHA256/size. It refuses if any of the
eight older/current diagnostic native processes is present. The historical
pending marker is recoverably archived only after explicit operator reboot
confirmation and exact capture/marker timestamp verification.

## One supervised test after staging

1. Physically reboot to load passive observer v6. Do not launch older probes.
2. Open Amazon Remote Navigation14. Press A once and release all buttons for the
   launcher; wait for IDLE health PASSED after15s of stable status.
3. Press A to arm. Put the remote in flashing pairing mode. Press ZL only when
   `Link_READY=1`; release it. Do not press A again or repeat the link attempt.
4. If READY and Pad1 appear, release all remote buttons and briefly press Down.
   Try navigation on HOME. After an owned-pad replacement, release before a new
   press so the triggering/held input is not replayed.
5. If stopped/timed out, do not retry. Open FTPD and report the LED/UI behavior.
   The terminal text log closes for capture. Return to FTP before another reboot
   if possible; root will correlate exact candidate, launch instance and timeline.

Only a device test can verify this new observer interface and callback layout in
the running system. Successful compilation, sanitizers and model tests are not
evidence of pairing or HOME navigation. No automatic reconnect, bond deletion,
filter/radio reconfiguration, wake support or public release is included.
