# Supervised next test and rollback

Perform these steps only during a supervised test with a working recovery route.
The candidate is not yet device-validated. First record your working probe and
passive observer hashes and preserve backups. Builds do not install anything.

## 1. Launch-only gate

1. Confirm Horizon22.5.0, passive observerv5, the exact target address, no other
   custom BLE client and no program-ID collision at0100000000a4d001. Review other
   installed modules for a PSC0xbe conflict; do not assume the ID is globally free.
2. Preserve current `link-test-pending.txt` **before** any action. A fresh reboot
   must be confirmed by the operator before explicitly recovering this prior attempt lock.
   Never remove it merely because the old probe exited or a timeout elapsed.
3. Copy/readback only the separate candidate module and control NRO. Back up any
   existing destination; refuse to overwrite unknown contents. No Hekate, Trinket,
   firmware, sys-patch, MC, save, pairing or network-blocking change is needed.
4. Deliberate supervised activation requires the exact text
   `enable-experimental-module=1` plus LF in
   `sd:/config/amazon-remote-module/enable.txt`, and a newly created empty
   `sd:/atmosphere/contents/0100000000a4d001/flags/boot2.flag`. These are omitted
   from build outputs; create them only after review and with an operator present.
5. After a user-operated reboot, launch the control NRO. Expect fresh IDLE status.
   No scan, link, controller attach or bond should exist from boot alone. Save
   `module.log` and status. If absent/stale, inspect launch/PSC/ACL results before
   any connection attempt. Do not broaden permissions or spoof ARUID blindly.

## 2. Neutral controller gate, before Bluetooth

1. While fresh IDLE, press physical Y in the matching alpha.2 control NRO. Expect
   Neutral_check=PASSED (service replies only), Pad_attached=0, cleanup result=0,
   last ACK mask=0 and two successful Set replies. Only neutral attach/detach is
   attempted; no remote pairing, advertisement or target-file setup is needed.
2. Preserve module.log and status. There should be no Bluetooth capture/link log
   from this check and no visible button-driven menu movement. Joy-Cons must keep
   working. A success reply is not proof of navigation or controller coexistence.
3. A second Y is ignored for this module instance. On failure, expect STOPPED,
   a preserved error and no A/ZL retry. Collect diagnostics before another test.
4. Only after this gate passes, proceed to the one explicit remote-output test.
   A successful check leaves IDLE; it does not consume a Bluetooth link attempt.

## 3. One background-output test

1. Make remote flash; physical A arms one capture. Wait until `Link_READY=1`;
   physical ZL requests exactly one link. ZL before readiness must do nothing.
2. Expect the same descriptor/security-request/report-reference/two-CCC sequence
   as the proven probe. Then HDLS attach+neutral state must return success.
   If ARUID0 transport or HDLS access fails, stop and collect; don't run a retry.
3. Exit the control UI with +. Within the90second bound, try remote D-pad,
   Confirm/A, Back/B, Home/HOME and Menu/+ in HOME. Record visible behavior and
   logs; success replies alone are not system-navigation proof. Keep Joy-Cons
   usable throughout. Voice/media/app keys should have no unintended action.
   Changed mapped inputs should produce HDLS ACK log lines; the last ACK mask
   and successful-write count are shown in the control UI. These are service
   diagnostics, not substitutes for observing the on-screen action.
4. Verify press AND release: no drifting selection or held Home after release.
   Use control X to stop; expect neutralize/detach/release and no repeat/rearm.
   A forced10second continuous mapped hold must stop cleanly. Capture failures
   and cleanup result codes rather than reporting assumed recovery.
5. Separately observe timeout/disconnection cleanup. Every attempted link retains
   its durable pending marker and still requires fresh reboot/recovery for a new
   diagnostic session. Do not run the old probe simultaneously.

## 4. Sleep/resume only after output and cleanup proof

1. Test actual Switch sleep while idle, then with the candidate pad attached.
   Monitor PSC request/ack ordering and ensure no stuck key or new Atmosphere
   fatal report. Verify Joy-Con wake and normal boot/controls remain intact.
2. On resume, status may return STOPPED; no automatic pairing/reconnection is
   permitted in this revision. Confirm the remote session stayed disarmed.
3. Only then design explicit persistent-bond/reconnection policy using new device
   evidence. Current generic NoMitm and pairing hints are not bond proof.
4. **True remote wake is a distinct test/research gate.** HDLS Home while CPU is
   awake is not sleep wake. A sysmodule cannot inject buttons while its CPU is
   actually asleep; controller-side/BT-firmware wake recognition must be proven.
   Do not substitute always-awake screen-off behavior without a user decision.

## Rollback

Power off/recover through the already-working Hekate route if necessary. Remove
only the newly created candidate `flags/boot2.flag` or move this candidate folder
to its recoverable backup. Keep the old working probe/MC untouched. Preserve
module logs, status and the pending link marker for diagnosis; don't clear any
pairing records or roll back NAND/firmware to undo this optional module.

No game installation, save restore or other unrelated change is part of this test.
