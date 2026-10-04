# Alpha.2 state diagnostic3 (local supervised tool)

**Amazon Remote State Check**, NACP `0.2.0-a2-diag3`, is a separate getter-only
app. It never starts/stops a process, attaches a debugger, installs hooks,
changes permissions, or calls Bluetooth/controller APIs. The installed module,
control apps, activation, NPDM, prior evidence and pairing state stay unchanged.

Exit FTPD, open **Amazon Remote State Check** in Homebrew Menu, wait 10 seconds,
press + to exit, then open FTPD. **No reboot or remote pairing.** It automatically
captures an initial and a roughly five-second snapshot. There are no action
buttons other than exit. If saving fails, photograph the screen; do not retry or
delete evidence. Existing or partial evidence files are never replaced.

## Getter provenance and limits

The adapter uses `pmdmntGetProcessId` for only `0100000000a4d001`, then the
[official Atmosphère copied-process-handle getter](https://github.com/Atmosphere-NX/Atmosphere-libs/blob/aae2da0155a490abd3bfd17a70ccd16bb73c916a/libstratosphere/source/pm/pm_ams.os.horizon.c)
(`pm:dmnt` command65000, 32-byte location/override-status output, HIPC COPY handle).
The layout/request is adapted under GPL-2.0-only from Atmosphère-NX; the temporary
handle is closed after inspection. Returned program identity must match before
calling `svcGetProcessInfo(ProcessInfoType_ProcessState)` (SVC0x7c). The syscall
is never attempted unless `envIsSyscallHinted` permits it. Permission/service
errors are saved verbatim and displayed as UNKNOWN, not a health claim.

States0..7 are Created, CreatedAttached, Running, Crashed, RunningAttached,
Exiting, Exited, DebugSuspended. **Running includes waiting/blocked threads**;
it is not proof main() was reached or that input works. This does not collect
PCs, registers, memory, or a thread list. A process can exit between getters.

The libnx `pmdmntGetJitDebugProcessIdList` getter retrieves at most32 exception
PIDs temporarily. The [PM implementation](https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/pm/source/impl/pm_process_manager.cpp)
selects `HasExceptionWaitingAttach`, without clearing it, and returns the number
copied, not the total. Only our PID membership and count/capacity-bound are
persisted; other PIDs are discarded. At capacity, not-found is inconclusive.
Membership is evidence of a waiting exception, not its root cause.

## Evidence

Exclusive files under `sd:/config/amazon-remote-module/`:

- `state-diag3-open.bin` (phase1)
- `state-diag3-5s.bin` (phase2)

Each is exactly112 bytes, little endian, magic `0x41525333`, version1.
Offsets: 0 magic/version/phase/reserved (four u32); 16 boot-ms/PID/location-program
(three u64); 40 init/query Results; 48 exception-list Result/count/found/capacity;
64 copied-info Result/identity-match/SVC-hinted/state-attempted; 80 state
Result/state-valid/close-attempted/close-Result (all u32); 96 raw state (s64);
104 reserved2; 108 FNV-1a checksum of bytes0..107. Reserved fields are zero.
`ffffffff` means NOT ATTEMPTED; valid/called flags must be considered alongside
Results. Files are flushed, fsynced, closed and read back without overwrites.

Host adapter tests use a fake SDK and do not prove device permissions. The real
SDK build verifies compilation/layout assertions only. Actual device Results
are required before deciding the next module change. This tool is not a public
release or functional remote-support claim.
