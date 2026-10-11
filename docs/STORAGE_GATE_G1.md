# Storage gate G1: no companion storage work during power transitions

G1 is S1's companion plus one rule, behind `ARMODULE_SLEEP_STORAGE_GUARD_G1`.
From the first non-FullAwake power request, or any failed request, until an
acknowledged FullAwake, the companion's main thread skips its maintenance
`Poll` and `PollHandshake`. The input module is unchanged (H1).

## Why

The companion acknowledges power transitions on the same thread that runs
three one-shot diagnostics: filter correction, the lease-scan trace and the
receiver writer. Each saves to the SD card once the capture lease ends. The A3
pause path ended the lease mid-sleep without stopping them, so a write could
block the next acknowledgement and stall sleep. This is the root cause recorded
in [SETUP_R3](SETUP_R3.md), and it was latent in the A3 path as installed.

Closing the gate in the companion's first power request also covers the module
ending the lease over IPC during its own sleep handling. On the device the
companion receives that first request about 70 ms before any module reaches
SleepReady.

## Device result, 2026-10-11

On a cold boot the remote connected at 13.4 s, and the console was slept with
the remote connected at 34.8 s, inside the live-lease window where the old code
wrote mid-sleep. There was **no hang**.

The receiver file for the first lease (armed at 13056 ms) was not written: the
gate held it through the sleep, and the wake flow then reset and re-armed the
lease. The second lease's file (`receiver-capture-52568.bin`) was written while
awake. The A3 wake flow ran through the gate as before: admission 315 ms and a
fresh session 629 ms after the FullAwake acknowledgement.

One survived sleep does not prove the race is gone; the mechanism and the
storage evidence support the fix. Filter-correction and scan-diagnostic files
may now be skipped for a cycle whose lease is reset by the wake flow. That is
expected, not a fault.

## Remaining reconnect blocker

That run did not reconnect after the wake. The C1 native peer record existed
with its device type reset from BLE to 0, and the C1 bridge never rewrites an
existing record, so preparation failed closed with `0xf0`. This is the same
failure as the second cycle in [WAKE_A3](WAKE_A3.md) and is unrelated to G1.
Across the day's wakes the type was reset on 2 of 4. A lasting fix needs a
reviewed C1 bridge rule for a stale record of the exact configured peer.
