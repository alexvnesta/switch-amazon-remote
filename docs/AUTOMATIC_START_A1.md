# Automatic startup A1 qualification

A1 reuses the hardware-verified C2 saved-key connection path. It changes the
input sysmodule and its status app only. The companion and native patch remain
byte-for-byte C2. No pairing, enrollment, key export, filters, firmware, or
controller-slot policy changes.

## Behavior

- Atmosphere boot2 starts the optional input module. It waits for PSC FullAwake
  and 15 seconds of healthy idle status before preparing the saved peer.
- Preparation and the single connection request happen without A/ZL or opening
  a homebrew app. A remote button still has to wake the remote itself.
- The unchanged C2 companion permits one lease/peer/encryption request per boot.
  An input-process relaunch cannot rearm it. A failed connection is not retried.
- An exclusive, timestamped A1 attempt journal replaces the manual global marker
  as this variant's audit trail. All old C2 markers and logs remain untouched.
- Once connected, the diagnostic 30-minute cutoff is removed. Setup deadlines,
  monotonic-clock checks, held-input watchdog, neutralization and owned teardown
  remain. Missing releases are not allowed to become indefinitely held buttons.
- Before any attempt, startup sleep transitions may pause/resume idle waiting.
  After an attempt, sleep, Stop, link loss or error remain terminal until reboot.
- The small status app only displays status and sends Stop from Joy-Cons across
  player slots. It cannot launch, connect, enroll, or re-pair.

## Not yet complete

This is the first physical gate toward roadmap step 3, not a production claim
that all steps 1 through 3 are finished. Same-boot reconnect and post-sleep
reconnect require reusable observer leases, new never-reused encryption callback
contexts, saved-record reload, and proven cancellation/absence at each boundary.
Those one-shot limits are intentionally not removed by an automatic retry loop.
In particular, a connection timeout with no owned handle is not proof that the
native request was cancelled. Remote wake of the Switch is also unproven.

## Test

Reboot once and remain on HOME, keeping the Switch awake. From HOME appearing,
tap remote Up every few seconds for up to 45 seconds. Do not hold Home, pair, or
open the old Setup workflow. If navigation works, test all four directions,
Select and Back. Open Amazon Remote, press Joy-Con X, wait for STOPPED, then +
and FTPD. Retrieve evidence before another iteration.

## Rollback

Remove only the input module's new boot2.flag to disable automatic startup.
Restore the preserved C2 input executable and Setup app using the deployment
receipt. The companion, native patch, saved bond and old C2 evidence are not
modified. A reboot is required to run restored code.
