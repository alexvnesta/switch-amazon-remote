# Setup sleep R3: retry after a missed first connection

> **Withdrawn 2026-10-11.** On its second device run, R3's new path hung the
> console while entering sleep and needed a manual power cycle. The console was
> rolled back to H1 and S1. Do not reinstall this build. See
> [device results](#device-results-2026-10-11).

R3 builds on H1 (A3 lineage). It changes one rule: a sleep while the remote is
still connecting no longer ends the module for the rest of the boot.

## Why

Device logs from 2026-10-11 showed the first connection after boot landing
anywhere from 13.7 s to over 30 s. The Switch lock screen sleeps on its own
about 31 s after boot when nobody touches it. A sleep before READY was terminal
by design ("sleep during initial setup remains terminal"), so a slow first
connection meant no remote until the next reboot.

## What changes

- **Module.** A sleep while the policy is `Scanning` or `Linking`, with the C1
  saved peer prepared, retires the unfinished attempt
  (`RetirePendingForSleep`). It makes no radio call and sends no companion
  Cancel. After the acknowledged FullAwake transition, the same two-view
  absence check as A3 runs, then one fresh saved-only attempt starts.
- **Companion.** `PauseSessionForPower` also accepts a prepared peer whose
  session never completed (`WakeSession::SleepPending`, keyed by the C1 peer
  nonce). It is idempotent across the repeated sleep-side power events and only
  pauses when a budget slot remains to resume into.
- **Wire.** The previously zero `reserved` field of `WakeStatus` and
  `WakeRequest` carries the wake kind: 0 for a completed session (unchanged A3
  wire), 1 for an unfinished attempt. Older peers reject nonzero, so a
  mismatched module and companion fail closed.
- **Hold.** The sleep teardown fails a pending connection about 20 ms before
  SleepReady. A failed poll while `Scanning`/`Linking` therefore waits up to
  1500 ms for a real sleep (`SETUP_R3_HOLD`), as A3 already does for READY link
  loss, before stopping.

The eight-session budget, saved bond, NPDM, native patch and boot
configuration are unchanged. There is no pairing, no new retry loop and no
remote wake.

## Evidence

Host tests under AddressSanitizer and UBSan cover the wake-session rules, the
production central runtime (cycles, live link, Cancel, terminal suspend,
unprepared peer, connected-but-not-ready, mixed with completed sessions) and the
production module transport, including a companion that reloads the saved
record over later polls. The A3 suites pass unchanged with R3 compiled in.

An independent review found one real defect before any device test: after the
wake command, the module still expected kind 1 while the companion correctly
reported kind 0 during the reload, so every R3 wake would have stopped. It is
fixed, and the realistic test fails with the fix reverted.

The module build was verified to be code-identical to installed H1 apart from
the R3 change; the companion to installed S1 apart from the R3 change.

## Limits

- If the existing native peer record has had its device type reset (the
  cycle-2 failure in [WAKE_A3](WAKE_A3.md)), preparation still fails closed.
  R3 does not change the native bridge.
- A pending connection request cannot be cancelled through public APIs. R3
  relies on the observed sleep teardown and confirms absence in two views
  before any new attempt.
## Device results, 2026-10-11

1. **Run 1.** The remote connected 12 ms after the boot request, so the
   lock-screen sleep found a READY session and took the unchanged A3 path. It
   reconnected about 7.8 s after the wake. This showed R3 boots and A3 still
   works, but did not exercise the new path.
2. **Run 2.** With the remote's batteries removed, the connection stayed
   pending. At the lock-screen sleep, R3 retired the attempt as designed and
   logged `SETUP_R3_SLEEP_PENDING`. The module closes its log before the PSC
   acknowledgement, so nothing further is recorded. The console then **hung
   entering sleep**.

This was the first time sleep was entered with a pending native connection
request while the companion was in its paused-session state. Under H1 the same
case took the terminal path and slept normally in several boots. The leading,
unproven hypothesis: the paused-session path leaves the filter-correction,
lease-scan and receiver runtimes unsuspended, unlike the terminal path. The
system sleep path must cancel the pending connection and wait for its
completion, and that wait may never finish. Any successor must either fully
suspend the companion's event-side runtimes for this case, or prove that the
pending-connection cancel completes, before another device test.
