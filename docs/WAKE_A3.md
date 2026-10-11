# WAKE A3 preview

This matched input-module and companion build targets the existing saved K7Q3M7
profile on the audited Horizon 22.5.0 setup. It is not new enrollment, a general
BLE driver, remote wake-from-sleep support, or a hardware-tested release.
The native C2 Bluetooth patch is unchanged. NPDM permissions are unchanged.

## Changes

- Startup readiness uses two seconds and four distinct valid status records,
  replacing the diagnostic fifteen-second/twenty-record soak only in A3.
- A separate 100 ms main-thread timer services the saved-peer/security runtime.
  General maintenance remains at one second. Idle ticks perform guard checks,
  not repeated filesystem or Bluetooth work after initial store loading.
- A completed saved-key input session can retire for sleep and resume after
  acknowledged FullAwake. Old client handles close and output is neutralized.
  Two system/user target-absence samples, at least 250 ms apart, and the exact
  old owner/serial/nonce/connection/epoch must match before another session.
- Confirmed loss of a previously READY link briefly neutralizes/detaches output
  while waiting up to 1500 ms for an actual PSC sleep event. Other errors stop
  immediately. Absence without sleep never authorizes a new connection.
- Callback contexts remain allocated and are never repurposed. Each session
  uses fresh nonces, re-reads the same saved record and repeats fingerprint,
  security and subscription checks. Numeric GATT connection IDs may recycle;
  they are not session identity. A reopened client queue is bounded-drained
  before requests, and notifications are ignored until fresh readiness.

## Limits

Eight successful sessions per boot is an explicit preview limit, bounding the
retained callback contexts. X Stop, shutdown, failed setup, ambiguous ownership,
cleanup failure or exhausted capacity still requires reboot. No auto-repair,
bond deletion, re-pairing or retry loop is included. Sleep during initial setup
remains terminal. The console must be awakened manually.

Host tests use synthetic radio/OS data. Server-side queue isolation across
client lifetimes and post-wake subscription behavior need device validation.
Lower configured waits are verified in source and tests, not a measured physical
press-to-response speedup. Verbose synchronous logging is still enabled.

## Next device test

Reboot with the matched build. Tap remote Up and test HOME navigation. Put the
console to sleep, manually wake it, then tap remote Up again. Repeat one more
sleep/wake cycle if successful. Do not hold remote Home or re-pair. At the first
failure, stop testing and open FTPD. If successful, open Amazon Remote, press
Joy-Con X, wait for STOPPED, press + and open FTPD to retrieve the logs.
