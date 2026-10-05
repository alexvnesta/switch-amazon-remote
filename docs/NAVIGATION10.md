# Navigation 10 supervised integration test

Navigation9 launched correctly, registered PSC, initialized the user BLE APIs,
and observed the configured remote's connection after a physical ZL request.
Its MTU512 request returned `0x0005528F`, before identity reads or HDLS output.
Later status publication failed, leaving a valid temporary record, and the module
exited. Application-context rejection and a file-reader/delete race are plausible
causes, not independently established meanings of those failures.

## Changes

- Explicit background BTM service for MTU and cached GATT services,
  characteristics and descriptors. The existing foreground probe keeps its user
  APIs. No borrowed/fabricated ARUID, driver initialization or global radio writes.
- Checksummed version3 status snapshots in a fixed-size module-owned file.
  Raw FS write/flush avoids deleting or renaming a file while the UI reads it.
  Readers reject stale or inconsistent snapshots. Open/read failures have a
  two-second maximum recovery window; partial writes, unexpected sizes/instances
  and invalid records are preserved and stop the test. Logs identify the FS step.
- Separate candidate `010000000000a4d4`, `/config/amazon-remote-nav10` and control
  app. Only its NPDM adds the public `btm` service and raises its own main-thread
  stack from 64 KiB to 128 KiB for bounded GATT inventory arrays. Old artifacts stay
  unchanged, with no boot flag and no automatic scan/connect/reconnect.

The thin system adapter follows the public libnx API and structure contracts:
[BTM](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btm.c),
[user wrappers](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/btmu.c).
No private MissionControl BLE source was used. The exact fingerprint,
NoMitm-requested security read, genuine property/ReportReference checks and
two whitelisted CCC subscriptions remain mandatory before controller output.

## Operator test

1. Physically reboot, open FTPD and tell the operator BEFORE opening this app.
   The old link lock must be recoverably archived after reboot confirmation.
   The launcher also checks that old/new native test processes are absent.
2. Open **Amazon Remote Navigation 10**. Release every button. Press A once, then
   release ALL buttons for one second. Wait for IDLE.
3. Release, then press A again to arm one 60-second capture window. Make the remote
   flash. Press ZL once only when `Link_READY=1`.
4. Wait for READY and `Pad_attached=1`. If either fails, do not retry. Exit with +
   and reopen FTPD so the exact failing API/FS step can be read.
5. On READY, exit with +, use Joy-Con HOME, and try short D-pad/OK/Back presses and
   releases. Record whether the HOME highlight moves. API ACKs are not proof.
6. Within the 90-second linked session, reopen the tool and press X to stop, or
   let the session expire. Exit and reopen FTPD.

Keep Joy-Cons available. A continuous mapped hold for 10 seconds stops and
neutralizes the pad. No sleep/wake, microphone or automatic reconnect is tested.
No firmware, Hekate/Trinket, Nintendo blocking, bonds, NAND or saves are modified.
Local SDK compilation and host tests are not actual device navigation proof.
