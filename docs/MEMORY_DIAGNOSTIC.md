# Alpha.2 RAM diagnostic4 (local supervised tool)

**Amazon Remote RAM Check**, NACP `0.2.0-a2-diag4`, reads the existing module's
40-byte RAM startup record. It does not replace/relaunch the module, attach a
debugger, change target memory/permissions, reboot, or call Bluetooth/controller
APIs. This is not a public release or functional remote-support claim.

Exit FTPD, open **Amazon Remote RAM Check** in Homebrew Menu, wait 10 seconds,
press + to exit, then reopen FTPD. **No reboot or remote pairing.** Two automatic
snapshots are taken about five seconds apart. If evidence saving fails,
photograph the screen; do not delete earlier files or retry until reviewed.

## Read boundary and admission

The exact startup-diag1 SD ExeFS must be142150 bytes with SHA256
`984a813aa00416a87dc38be88de017c27b859a1afbd2822ef88a9290089e1580`.
The adapter queries only program `0100000000a4d001` and verifies the copied PM
handle's returned program identity. All four needed syscalls must be hinted:
MapProcessMemory0x74, UnmapProcessMemory0x75, QueryProcessMemory0x76,
GetProcessInfo0x7c. Service/permission failures are preserved, never treated as
absence or a successful read.

Memory-region metadata enumeration is bounded to128 regions and a39-bit address
space. Only an RX code region of the pinned ELF's exact0x2b000 text extent is
eligible. The code reader verifies64 bytes at relative0x1af0 and the36-byte GNU
build note at0x33590, including build ID
`e585db4ac0ed0e5c5ecc8c79768d6b38182f790e`, before reading the checkpoint at0x35000.
No generic process dump, foreign-PID enumeration, stack/register collection or
remote-identity collection occurs. These admission checks are diagnostics, not
an adversarial security boundary against concurrent binary replacement.

The [Mesosphere mapping implementation](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/svc/kern_svc_process_memory.cpp)
maps a source code page into the caller's address space without changing source
page permissions. The temporary alias is RW-capable at the kernel level, but the
reader only uses const volatile byte loads, never writes through it. It queries
and validates page type/permission/attributes/range before mapping. Each mapping
is exactly4096 bytes and immediately unmapped after copying only the allowed
bytes. No source memory mutation or debugger pause occurs. Unmap failure stops
the read; the alias is reserved until app exit instead of reused. Do not interpret
a failed cleanup as a successful checkpoint read.

The checkpoint is read twice immediately for equality, then validated using its
checksum. Equality is not an atomic transaction; the module can advance during
inspection. A coherent stage0 with the known magic means no completed SM
checkpoint yet; it does not distinguish an SM wait from pre-checkpoint runtime
work. A valid stage records the last completed checkpoint, not a proven location
of the next blocking instruction. Running does not imply healthy execution.

The state-reader sentinel bug is corrected locally: libnx's pinned
`svcGetProcessInfo` assembly stores only w1 despite the s64 output signature, so
the output is zero-initialized before the syscall. The host model now reproduces
that four-byte store. Existing on-console State Check files are unchanged.

## Evidence layout

`memory-diag4-open.bin` and `memory-diag4-5s.bin` are exclusively created under
`sd:/config/amazon-remote-module/`, flushed/fsynced/closed, and fully read back.
Existing or partial files are preserved. Each is192 bytes, little endian:

- 0: magic0x41524d34/version1/phase1or2/reserved (four u32).
- 16: boot-ms/PID/code base (three u64).
- 40: PM init/query/copied-info Results; identity/hint-mask/hash-verified (six u32).
- 64: memory-query Result/regions/candidates/code-matches (four u32).
- 80: code map/unmap, note map/unmap, record map/unmap (six u32).
- 104: fingerprint/build-note/copied/coherent flags (four u32).
- 120: process-state Result/valid (two u32), then raw state (s64 at128).
- 136: unchanged StartupRecord40 bytes.
- 176: close Result/startup-valid/reserved2/checksum (four u32).

Checksum is FNV-1a over bytes0..187. `ffffffff` means NOT ATTEMPTED. Page-result
fields can contain the preceding QueryProcessMemory failure as well as a mapping
Result; successful copying and cleanup flags are necessary to interpret them.
Guard rejection/allocation failure does not invent a Horizon Result.

Host adapter ASAN/UBSAN tests and a real SDK build verify policy/compilation,
not actual device syscall permissions. Stop for device evidence before changing
the module initialization or attempting further pairing.
