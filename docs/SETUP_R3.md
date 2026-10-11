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

## Root cause of the run 2 hang

The companion handles power transitions, its 1 Hz maintenance `Poll` and its
100 ms handshake poll on **one** main thread. Three one-shot diagnostics on that
thread write to the SD card once the capture lease reads Ended (or, for the
receiver writer, Expired): filter correction, the lease-scan trace and the
receiver writer. The first two are opted in on this console.

- `CloseForPower` (the terminal path) ends the lease and, in the same
  power-handler call, suspends all three. `Poll` cannot run in between, so no
  storage work happens during sleep.
- `PauseSession` (A3 completed sessions and R3 unfinished attempts) ends the
  lease and returns **without suspending them**. The next `Poll` then starts SD
  writes in the middle of the sleep sequence. A write that blocks there keeps
  the main thread from acknowledging the next sleep state, and system sleep
  stalls.

This breaks the companion's own rule: no filesystem work before the power
acknowledgement.

Device evidence: each boot that arms a capture leaves one
`receiver-capture-<arm ms>.bin`. Run 1 (arm 12972 ms) and the boot after the
hang (arm 13114 ms) have theirs. The hung run 2 (arm 12849 ms) has none. Both
runs ended the lease at sleep (31.0 s and 32.5 s); run 1's writes completed,
run 2's did not. That fits a race between `Poll` and storage suspension. The
exact blocking call is not observable, because both logs close before sleep.

Earlier sleeps were safe for identifiable reasons. H1's terminal path suspends
the writers. The A3 cycle tests first slept at 147.9 s, after the 60 s lease
had expired, so the writers had already saved while awake.

**The same gap exists in A3 as installed (H1 with S1).** A connected-remote
sleep before the boot lease expires (about the first 73 s after boot, such as
the lock screen sleeping 31 s after a quick connect) takes `PauseSession` with
a live lease. R3 run 1 was exactly that sleep, and it survived.

Fix direction: suspend the three writers in `PauseSession` exactly as
`CloseForPower` does, before the acknowledgement. They are diagnostics only,
and the link-only reconnect after wake uses none of them.
