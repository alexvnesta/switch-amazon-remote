# STARTUP A4: cold-boot admission

A3's cold-boot trace passed idle health but received no PSC request until the
user put the console to sleep. Its `full_awake=false` startup gate consequently
prevented any initial attempt. The subsequent wake event enabled the first
connection, which reached input readiness in 4.046 seconds from automatic ARM.
That test did not exercise reconnecting a previously READY session after sleep.

A4 changes only the input module and the status UI identity. The A3 companion,
native patch, permissions, saved pairing, transport, session limits and SD
namespace are unchanged. A successfully registered input module starts in an
operational bootstrap state, matching the lifecycle convention used by
[MissionControl Main](https://github.com/ndeadly/MissionControl/blob/master/mc_mitm/source/mcmitm_main.cpp).
This is not a queried system FullAwake snapshot and cannot admit sleep recovery.
The two-second/four-publication idle health check and all existing radio,
capture, saved-peer, security and controller preflights still apply.

The first observed PSC event irrevocably removes bootstrap eligibility before
GetRequest. Every transition blocks automatic ARM/CONNECT until FullAwake has
been read and successfully acknowledged. Request/acknowledgement errors,
shutdown and unknown states cannot grant admission. Pending transitions are
processed before automatic commands. Session reset never reconstructs the
bootstrap grant. Explicit Stop and failed setup remain terminal for the boot.

Tests cover notification-free startup, a pending transition on the first loop,
each non-awake state, error paths, acknowledgement ordering, wake denial under
bootstrap, and no revival of a stopped attempt. Host tests and compilation do
not prove real console startup or sleep/wake reliability.

Physical test: reboot, stay on HOME and tap remote Up without first sleeping.
If navigation works, sleep and manually wake the console once, then tap Up.
Stop via Amazon Remote / Joy-Con X, wait for STOPPED, and open FTPD for logs.
Do not hold remote Home or re-pair.
