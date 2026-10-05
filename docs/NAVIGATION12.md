# Navigation 12: stability before pairing

This candidate addresses the native startup regressions before asking for another
remote test. It is not a release or a claim of working native remote support.
Navigation11 was not uploaded. Navigation10's captured attempt exited in status
initialization before Bluetooth setup; its files remain preserved.

## File handling and admission

- The first checksummed status snapshot explicitly initializes a file created by
  this writer; allocated bytes need not be zero. Preexisting files are refused.
- The UI opens status at most four times per second, not fifty. It distinguishes
  the exact sharing-lock result from corruption or other errors. Only a previously
  verified snapshot still within the two-second freshness window may survive a
  temporary sharing lock; commands recheck freshness every frame.
- The writer staggers sharing-lock retries over short intervals for at most two
  seconds. Failed writes, corrupt previous records and other FS errors stop the
  test. No delete, rename, truncation or partial-write retry is used for status.
- Both module and UI require 15 seconds of distinct successful IDLE snapshots
  before ARM is allowed. This barrier performs no Bluetooth initialization, scan,
  pairing or controller attachment. Gaps, errors, state/instance/clock changes
  invalidate the gate. No additional reboot is needed between a passing barrier
  and the single remote attempt in that same module instance.
- The UI waits for the previous command acknowledgement and refuses to replace
  a still-pending command file, avoiding another reader/delete race.

## Native Bluetooth request path

MTU and metadata use the public system BTM service. Target-checked GATT requests
use public driver commands90/91/93/94/95 without an AppletResourceUserId. The normal
`btdrvInitialize()` helper sends a driver-initialization command, so this adapter
instead obtains only its own service handle with `smGetService`.

The whitelist permits PnP/report-map reads, input ReportReference reads, the two
selected input notification registrations and standard CCC0100 writes. Existing
fingerprint, security-read, genuine-property and fresh target/handle checks remain
required. No application-ID borrowing, auth downgrade or alternate subscription
fallback was added. No driver initialization, global driver-event consumption,
radio/filter changes, explicit bond deletion or vendor/voice writes are used.

The existing per-client `bt` event queue and managed scan/connection path remain.
Those wrappers still carry the native application's zero resource-user ID;
successful setup in Navigation9 does NOT prove native response routing. In
particular, driver requests reaching that queue remain unverified on hardware.
Missing completion events stop at the existing deadlines, with exact request
Results logged. API acceptance is not connection, encryption, bond or navigation
proof.

Public API/wire references (libnx Authors, ISC-licensed source):
[driver](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btdrv.c),
[client event queue](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/bt.c),
[system metadata](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btm.c).
No private MissionControl BLE source was used.

## Local acceptance and remaining boundary

Production writer/reader/scheduler/gate code passes 300 deterministic sharing-lock
phase/hold scenarios, including transient contention and bounded sustained-lock
failure. Native request tests inspect exact packet sizes, IDs, padding, buffers
and command whitelist, reject app-path fallback, and cover per-request failures,
service-open failure, missing completions and decoded button input. Existing
regressions and SDK builds must pass with sanitizers and warnings-as-errors.
These are models and compilation checks, not Horizon scheduling or event-routing
proof. C++ startup, physical radio responses and visible controller input still
need the supervised device test below.

## Operator test

1. Once staging is verified, physically reboot, open FTPD, and tell the operator
   before opening the app. The operator checks the staged files and absence of a
   pending link lock; the launcher independently checks old/new native processes
   and uptime before its one manual launch.
2. Open **Amazon Remote Navigation 12**. Release all buttons, press A once, then
   release ALL buttons for one second. Leave the remote alone.
3. Wait for IDLE and `IDLE health: ... PASSED` (at least 15 seconds). If it never
   passes, do not flash/pair the remote. Exit with + and reopen FTPD; the startup
   result and status read/write step can be inspected without a pairing attempt.
4. On PASSED, press A again, make the remote flash, and press ZL once ONLY when
   `Link_READY=1`. Wait for READY and `Pad_attached=1`. If either fails, do not
   retry: exit with + and reopen FTPD.
5. On READY, exit with +, use Joy-Con HOME and test short remote D-pad/OK/Back
   presses/releases. Report whether the HOME highlight actually moves.
6. Within the 90-second linked session, reopen the tool and press X to stop, or
   let it expire. Exit and reopen FTPD for logs.

Keep Joy-Cons available. Continuous mapped holds of 10 seconds stop/neutralize the
pad. The candidate has no boot flag, automatic scan/connect/reconnect, wake or
microphone claim. Firmware, NAND, saves, Nintendo blocking, Hekate/Trinket,
controller bonds and older candidates are not changed.
