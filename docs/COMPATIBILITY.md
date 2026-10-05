# Compatibility and evidence boundaries

This is a single-device research result, not broad Fire TV remote compatibility.
Tested model: K7Q3M7; device information VID0171/PID0427; HID Report Map149 bytes.
Firmware: Horizon22.5.0 on a first-generation Switch with Atmosphere sysMMC.

The successful probe0.1.10 session observed:

- Exact device-information and report-map fingerprint match.
- Five GATT Report characteristics; all ReportReferences resolved.
- Keyboard input: ReportID1, instance0; consumer input: ReportID2, instance3.
- Two validated notification registrations and standard CCC `0100` writes.
- 109 decoded reports: 53 keyboard, 56 consumer.
- 34 presses and 34 releases across12 named buttons.
- More than44 seconds connected after INPUT_READY; final held-button mask zero.
- No new fatal-dump filename relative to the local pre-test baseline.

Button counts were Up7, Down2, Back2, Home3, Menu2, FastForward2, PlayPause2,
Rewind2, Voice1, Right3, Confirm6 and Left2. Voice here means a decoded **button**,
not a working microphone. Counts describe this test, not a completeness claim.

A matched NoMitm-requested GATT read succeeded, but neither generic pairing
hints nor successful IPC proves encryption or a durable bond. Background ARUID0
access and HDLS system navigation were subsequently reached in Navigation14 as
described below. Durable bonding, actual sleep/resume, radio ownership and remote
wake remain separate hardware validation gates.

## Background Navigation14 device evidence

One manually launched Navigation14 session on the same hardware/firmware reached
two validated input subscriptions and an owned HDLS pad. The operator reported
successful HOME navigation. Device records show86 successful Set replies,
including initial/final neutral and42 mapped neutral releases. Changed inputs
included all four directions, Select/A, Back/B, Home/HOME and Menu/+.

The fixed90s connected guard stopped the session, not a crash. Final neutral,
detach and work-buffer release all returned success, with the owned page freed.
Every observed owned-pad presence query succeeded and found the handle. Therefore
the confirmed-absent replacement branch was not exercised by this success.
This is one successful trial; earlier connection timeouts are not causally solved.
Sustained use, durable pairing, reconnect, native-menu enrollment, other models
and sleep/wake remain unverified. The full private device logs are not published.

## Public-SDK layout observation

In this session the BTM characteristic-property member at offset0x1e was zero,
whereas raw offset0x20 agreed with getter-only public driver properties for all
five Report instances: `1a,1a,0e,1a,1a`. The transport does **not** globally change
the SDK layout or invent Notify bits. It cross-checks complete UUID/instance sets
through public getter IPC80/81, after exact target/fingerprint/security-request
gates. Ambiguous, conflicting, foreign or missing data fails closed.

This suggests a current-firmware layout mismatch; it is not proof of a universal
ABI change. Other firmware and remote models need their own reviewed evidence.

Raw logs, remote MAC addresses, account details and console crash dumps are not
published. Unit tests use synthetic locally administered fixture addresses.
