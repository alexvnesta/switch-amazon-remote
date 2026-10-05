# Navigation16: same-session enrollment context

This experimental candidate preserves the Navigation15S navigation path and
replaces its operator Y action with a getter-only diagnostic. It does **not**
call either PairDevice API. It is not a bonding fix or production daemon.

The foreground Enrollment Context app saw the target in the system connection
list, but not in its new user client's list after the navigation module stopped.
The user getter carries an applet resource user ID and PID. That later snapshot
cannot explain the earlier module's BTM error2143-0431 by itself.

Navigation16 records the same getters inside the still-running navigation
session. It borrows that session's already-open user and system clients without
reopening or closing them, and opens/closes only a temporary `btm:sys` getter
client. It reuses the tested audit collector and SDK adapter.

## Test after the operator-confirmed reboot gate is recovered

Do not run old launchers or probes alongside it. The NSP has the same temporary
program ID as Navigation15/15S, but a new evidence namespace and launcher hash
pin. Staging does not launch it, add a boot flag or clear the pending marker.

1. Open **Amazon Remote Navigation16**. Press Joy-Con A once and release all
   buttons to launch IDLE. Wait for stable IDLE as prompted.
2. Press A once to scan. Put the configured remote in pairing/discoverable mode.
   When Link_READY is shown, press Joy-Con ZL once. Do not repeat it.
3. Wait for READY and Pad_attached=1. Release every remote button. Press Joy-Con
   Y once to save the getter-only context. This sends **no pairing request**.
4. When Context SAVED appears, press X to stop. Wait for STOPPED/Pad_attached=0,
   then + to exit and open FTPD for capture. Do not relaunch the module.

The output is an exclusive168-byte checksummed record with module instance,
expected handle, before/after target-binding checks and the address-free128-byte
audit snapshot. Short/corrupt/unsynced records remain preserved and cannot be
overwritten. Query failures remain visible, not silently converted to absence.

The paired-list parameter remains an AD-derived experiment, not a proven vendor
enrollment schema. Same handles do not establish client ownership; native
gamepad pairing state is not a BLE permission. List inclusion would not establish
encryption or durable bonding. No radio/filter/ownership override, unpairing,
fake HID registration, key reads, automatic pairing/reconnect or wake exchange
is introduced. Firmware guard and module NPDM permissions are unchanged.
