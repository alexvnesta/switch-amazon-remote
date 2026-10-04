# Alpha.2 startup diagnostic1 (supervised local revision)

This is not a public release or verified fix. The last released version remains
v0.2.0-alpha.2. The startup candidate keeps its existing NPDM, service permissions,
program ID and protocol2. It disables ARM/LINK in both the module and control UI;
only neutral Y and stop X remain available after a fresh status is established.
No Bluetooth initialization, scan, pairing or link is permitted by this revision.

## Added evidence

- Raw FS checkpoint before settings/devoptab-dependent normal logging:
  `sd:/config/amazon-remote-module/startup-diag1.bin` (40bytes).
- Read-only PM process lookup by the control app; no launch/start/terminate API.
- Control app snapshot for FTP retrieval:
  `sd:/config/amazon-remote-module/control-diag1.bin` (72bytes).
- Normal `module.log` and `status.bin` remain in the module's directory.

The raw checkpoint is checksummed and fixed-size. A reader rejects malformed
records. This is not a crash-atomic filesystem transaction. Failed raw FS writes
cannot diagnose themselves through the same failed storage channel. SM/FS/SD
failures before SD access, loader rejection and pre-appInit runtime failures can
still leave no checkpoint. A failed PM query is not proof that the process is
absent: preserve its exact Result. PM process presence is not module health.
A saved checkpoint is historical unless its instance matches fresh live status.

The control app records on first observation and on changes, at most20writes per
launch. Failure preserves any existing publication temp rather than deleting it.
Runtime checkpoint writes are bounded to startup, first status or status failure;
this is not a repeating SD write every polling tick.

## First user test

1. After reviewed transfer and readback, reboot manually into Atmosphere.
2. Open **Amazon Remote Startup Check** (NACP0.2.0-a2-diag1) from Homebrew Menu.
3. Leave it visible for10seconds. Do not press A/ZL/Y or pair the remote.
4. Exit with + and open FTPD. Retrieve the three evidence files above.

Do not run the old foreground probe alongside the active module. Preserve the
pending-link marker, pairing database, observer, boot chain, and network blocking.
The neutral controller check is a separate later test, not this startup test.

## Rollback

Preserve full readback-verified copies of both old binaries before replacement.
The supervised private deployment receipt identifies the exact SD backup folder.
If the new candidate affects boot, use Hekate USB SD access and remove only
`atmosphere/contents/0100000000a4d001/flags/boot2.flag` to disable this candidate.
Do not change Hekate/Trinket or other module activation. Restore only this module
and its control NRO from the exact rollback copies if needed; preserve evidence.
