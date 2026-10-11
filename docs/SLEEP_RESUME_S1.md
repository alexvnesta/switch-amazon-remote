# Passive sleep/resume S1

S1 observes one user-operated sleep/manual-wake cycle. It does not enable remote
wake or change filters, scanning, bonds, native Bluetooth firmware, boot entries,
overclocking, controller output or Nintendo network blocking.

## Completed physical observation

The one requested manual cycle completed on 2026-10-10. Its fixed 20,080-byte
report and 16-byte completion marker were fully read back and verified locally.
The decoder reports `PSC_sleep_resume_observed`: successful request and
acknowledgement pairs reached the selected sleep state at 32,948 ms, reached the
selected awake state at 36,927 ms, and finished its post-resume observation at
47,000 ms. All recorded queue-drop counters were zero.

There were no IPC or BLE records in the post-resume phase. That is an observation,
not proof that controller filtering was retained or absent. The active companion
and unchanged native package both matched their pinned hashes on full readback.
The exact opt-in was then recoverably moved to the S1 recovery stage, so the
observer is disabled for future boots. The physical receipt is retained privately
with the report. This result does not establish deep-sleep wake, Home advertising,
controller-side wake matching or Amazon-remote wake.

## Scope and ordering

The source-owned companion uses existing callbacks only:

- Startup reads the strict `sleep-resume-s1\n` opt-in before producer launch.
- The existing IPC producer records HID/BTM scan/filter request metadata. Filter
  add/delete taps run before the existing correction/forwarding path. Original
  forwarding, results and correction behavior remain unchanged.
- The existing BLE producer copies seven pre-address scan bytes or eight
  result/action completion bytes. No peer address, name, AD payload, mask data,
  UUID or bond/key bytes are retained.
- The existing Main/PSC thread records requests and acknowledgement results.
  It does not add a power client, acknowledge requests itself or call the old
  power-policy adapter. Persistence uses the existing Main maintenance timer.

HID/BTM request metadata consists of command, program, reported size and, for
exactly sized filter requests, index, advertisement size/type and mask size.
No driver completion is attributed to that request. Non-HID/BTM requesters and
internal driver paths bypassing these IPC methods are outside this capture.

Each IPC/BLE producer has three separate 64-record SPSC queues. The first
successful sleep-state request selects the sleep-transition partition. A
successful awake acknowledgement selects the post-resume partition. Startup
traffic cannot consume these later partitions. PSC metadata has 32 slots. All
queues have bounded drops and no allocations, waits, locks or filesystem work
in their producers. The partitions describe **observed PSC ordering**, not
measured radio or hardware power states.

The window expires ten minutes after startup. The first acknowledged resume
starts a nonrenewable ten-second post-resume observation interval. No storage
is permitted after an observed sleep/error/shutdown request until a successful
known-awake acknowledgement. A second sleep blocks storage again; it never
renews the interval. PSC handling and persistence share Main, so no PSC
acknowledgement runs concurrently with the writer. The existing FS power
dependency and power policy remain unchanged.

## Report and failure behavior

The fixed report is 20,080 bytes. It is held in static storage, not Main's 4KiB
stack. The one-shot writer uses exclusive creation, flushes the report and
compares every byte on readback. Only then may it create the 16-byte `.ok`
marker. Existing files, failed and partial writes are retained. There is no
automatic retry, rotation or rearm within a boot.

Reports are saved under `/config/amazon-remote/sleep-resume-s1-<start_ms>.bin`.
The source-only decoder in `scripts/read_sleep_resume_trace.py` validates size,
schema, checksum, counts, record privacy bounds and the optional readback marker.
An `Expired`/`PowerError` verdict is not a successful cycle. Drops and absent
markers must remain visible during triage. Late producer events/counters can be
outside the final snapshot, so it is not an exact event ownership ledger.

The opt-in persists across reboot until archived. Every boot may produce at most
one report, but no existing report is overwritten. Remove/archive the opt-in
after the requested test rather than leave a recurring diagnostic enabled.

## One physical test

After the reviewed companion and opt-in are installed with full readback:

1. Reboot normally into the existing default Atmosphere entry and reach HOME.
2. Within ten minutes, put the Switch to sleep using its physical power button.
   Leave it asleep for about twenty seconds.
3. Wake it with the physical power button or an already-working native controller.
   Stay awake on HOME for fifteen seconds.
4. Open FTPD and report completion. No diagnostic app, A/ZL input, re-pairing or
   Amazon-remote wake attempt is required.

Do not use the older Passive D2 reader for S1. Its retained manifest pins the
previous companion and is intentionally not updated or repurposed by S1.

If boot fails, use Hekate SD UMS. The deployment receipt identifies the exact
new stage containing the previous companion and recovery instructions. Preserve
the S1 files in unused quarantine paths, restore only that previous companion,
and archive the S1 mode file. Leave the native NSP, flags, patches, boot/OC and
pairings untouched. Never use older D1/L1/L2 withdrawal tools against this build.

## What a successful report cannot prove

It can establish the companion's observed PSC sequence and selected host-visible
scan/filter traffic around manual resume. It cannot establish controller filter
ownership, complete filter retention in deep sleep, internal LLR behavior,
Home-specific advertisements, GPIO/IRQ assertion, remote wake, sleep current or
reconnection reliability. No native firmware is executed off-device in its tests.
