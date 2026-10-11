# HID Information H1: capability observation, not wake support

This optional diagnostic build retains A6 connection, saved-key security,
input, Stop and sleep behavior. The normal A6 build does not contain this read.
H1 is an off-device candidate until a separate deployment receipt exists.

After a normal fingerprint-verified and security-qualified input session reaches
READY, wait at least five seconds and for no held button. The module requests
the HID Information characteristic (`0x2A4A`) exactly once per process, using
the existing owned GATT connection and notification path. It does not block
readiness or HDLS output on that request. The attempt is not repeated on
sleep/resume, failure, or timeout. This is a value read, not a write to enable wake.

The characteristic must be uniquely discovered within the existing primary HID
service. Its instance and handle come from the existing post-security inventory;
there is no fixed-handle fallback or extra discovery query. Declaration properties
are not treated as its value. The read uses authentication-request zero on the
already-qualified link, with no security escalation or fallback. No new pairing,
bond modification, scan, protocol-mode write, Control Point write or host wake
configuration is added. As with any GATT read, actual device behavior still needs
verification.

Only a read response matching the current owned connection, local generation,
HID service, and characteristic within three seconds can supply evidence. Input
notifications continue normally. Error status, wrong length, timeout, unsolicited,
late or retired responses do not produce capability claims or a retry. Existing
transport checks still handle lost ownership or structurally invalid SDK events.

## Evidence

`HID_INFO_H1_VALUE` records exactly four bytes, the HID version, country, the
raw flags, `RemoteWake`, `NormallyConnectable`, and other bits without interpreting
them. Every valid-value line explicitly says `host_wake_proven=0`. Failures use
`HID_INFO_H1_NO_VALUE`, `HID_INFO_H1_TIMEOUT`, `HID_INFO_H1_SKIPPED`, or
`HID_INFO_H1_UNAVAILABLE`; none means the remote lacks wake capability.

[Bluetooth HID Service v1.0, section 2.10](https://devzone.nordicsemi.com/cfs-file/__key/support-attachments/beef5d1b77644c448dabff31668f3a47-d2e31a4fe5fd4955b77461e3188e06af/HIDS_5F00_SPEC_5F00_V10.pdf#page=20)
defines the four-byte value. Bit 0 describes the peripheral's wake-signal capability;
bit 1 describes advertising when bonded but disconnected. Neither enables or proves
Horizon receiver-side wake support. The value may be cached for the bond lifetime.

## Why this is the next measurement

[tvbox's Fire TV documentation](https://github.com/Andy1210/tvbox/blob/main/docs/firetv-remote-ir.md)
reports silent disconnected sleep and benefits from holding a connection on a
Remote Pro, PID `0x0425`, not our K7Q3M7 PID `0x0427`. The author also describes
Fire OS retaining background connection intent. These are useful hypotheses,
not evidence that the same host policy is available in Horizon.

[fire-remote-client](https://github.com/justanotheruseer/fire-remote-client)
documents pairing, HID and privacy for different 4K/Lite models.
[Staars' BLE FireRC example](https://github.com/Staars/berry-examples/blob/main/ble/ble_fireRC.be)
reads HID Information and handles reconnects. Neither source inspected here
demonstrates K7Q3M7 waking a suspended original Switch. No third-party code,
pairing script or provisioning procedure was imported or run.

The public [libnx awake-request event](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/btm.h)
exposes an event handle, not an API for programming a peer/report wake filter.
Do not treat acquiring it as enabling wake. Receiver offload and connection
lifetime remain separate unanswered questions; H1 does not keep a session alive
through suspend or alter any power setting.

## Operator test after reviewed deployment

1. Reboot once. Stay awake on HOME and tap remote Up until navigation works.
2. Release all remote buttons and wait ten seconds. No A, ZL or pairing hold.
3. Open Amazon Remote, press Joy-Con X, wait for STOPPED, then + and FTPD.

Do not test remote-triggered sleep wake with this build. It does not implement it.
No additional homebrew icon is needed; the existing control app can show H1.

Host tests cover all 256 flag values, malformed lengths, missing/ambiguous metadata,
instance bounds, errors, timeout, foreign/late responses, Home input during the
read, and pending-read retirement across a synthetic sleep/resume. They do not
prove radio behavior or any physical wake capability.
