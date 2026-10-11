# Automatic startup A2: repeated-boot status repair

A1's first automatic saved-key connection and navigation worked on hardware.
The following boot failed before Bluetooth: the one-shot `StatusWriter` rejected
the prior instance's existing 88-byte `status.bin` at step4 with synthetic0x402.
The status file and HDLS trace still described the previous successful process.

A2 uses the SAME `/config/amazon-remote-auto-a1` directory. A fresh directory
would mask this bug, not fix repeated boots. Before its first status publication,
A2 validates a prior record, rejects the same instance, renames it without
overwrite to `status-<old-instance>-before-<new-instance>.bin`, verifies all bytes,
and lets the unchanged strict status writer create a fresh file. Existing
archives, malformed records, ambiguous renames and partial writes are preserved,
not overwritten. An interrupted rename before new-file creation can recover next
boot. A partially initialized/corrupt status still requires investigation.

The status app now requires a matching, successful startup instance before
showing a status or sending Stop. A recorded startup failure is shown separately.
Module/status app version is A2. Log lines inherited from A1/C2 may retain their
historical labels; the new startup and archive lines explicitly identify A2.

The Bluetooth transport, native patch, companion, keys, pairing, power handling,
one-attempt-per-boot restriction, HDLS mapping and permissions are unchanged.
This repair does not implement retries or reconnect after connected sleep.

## Test

Reboot and stay awake on HOME. Tap remote Up periodically for up to45seconds.
If it connects, reboot a second time and repeat. If either attempt fails, do not
keep rebooting or pair again: open FTPD. After success on the second boot, open
Amazon Remote (AUTO A2), press Joy-Con X, wait for STOPPED, then + and FTPD.

Host tests exercise the real status writers across sequential instances on one
simulated filesystem, each IO failure, preservation/collision/race checks,
process interruption at rename/create/write, and UI instance validation. SDK and
host checks are not proof of physical repeated-boot connection reliability.
