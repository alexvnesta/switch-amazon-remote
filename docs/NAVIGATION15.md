# Navigation15: sustained background test

Navigation14 has one device-confirmed HOME-navigation session. All four
directions, Select, Back, Home and Menu produced successful HDLS writes. Its
intentional90s limit stopped that session with successful pad cleanup. This is
not evidence of durable bonding, reconnects, wake or every remote model.

Navigation15 keeps that connection/security/decoder/controller implementation.
The only behavioral extension is a fixed30-minute budget after successful Ready.
Setup still expires90s after observing the target connection, with the existing
shorter operation/discovery deadlines. Late setup completions cannot earn the
extension. Activity never renews it. The10s held-input watchdog, exact target/
handle checks, one-shot connection marker, power-event cleanup and one permitted
confirmed-absent HDLS replacement remain unchanged. No auto reconnect is added.

Identical successful connection-list and owned-HDLS health logs are sampled
every30s. Service polls remain500ms and250ms respectively. Errors, target loss,
absent owned pads, state changes, input reports, button/write receipts and cleanup
remain diagnostic. Log rotation remains bounded at256KiB per file plus two
backups; old logs can rotate out during heavy use. Status heartbeat remains500ms.
No microphone/vendor payload is collected. Passive observer v6 remains unchanged;
its callback capture lease remains60s and is not extended by this candidate.

Candidate ID `010000000000a4d9`, config `config/amazon-remote-nav15`, and exact
binary-pinned launcher are separate from the working Navigation14 artifacts.
No boot flag, firmware/network/radio/filter/bond/save change is included.

## Test after root confirms safe marker recovery

1. Physically reboot; leave FTPD open and tell root so the previous pending
   marker can be recoverably archived. Staging by itself does not clear it.
2. Open Amazon Remote Navigation15. A once, release all buttons for launch.
   Wait for15s IDLE health PASSED. A to arm, flash the remote, then ZL only
   when `Link_READY=1`. One attempt; don't press A/ZL again.
3. At READY/Pad1, + exits the control app. Navigate HOME with brief separate
   presses, leave all remote buttons released for3min, then navigate again.
   Keep the console awake; suspend stops this one-shot session by design.
4. Open the SAME Navigation15 control app again, without A/ZL. Press X to stop,
   then + to exit. Open FTPD so the closed log and final cleanup can be captured.
   If the app says STOPPED already, just return to FTPD; do not retry.

At30min Ready ends automatically if X has not stopped it. Unexpected loss also
stops/neutralizes rather than reconnecting. Only a device test proves longer use;
host simulation and SDK builds are not device navigation/reconnect evidence.
