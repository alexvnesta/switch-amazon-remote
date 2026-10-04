# Alpha.2 layout diagnostic5 (local supervised tool)

Open **Amazon Remote Layout Check**, wait 10 seconds, + exit, then open FTPD.
No reboot, launch, debugger attachment, remote pairing or other action buttons.
If evidence saving fails, photograph the screen and preserve all files.
Old installed apps, snapshots, module binary, activation and pairing state are
unchanged. This is not a public release or a proven startup fix.

## Reader correction

Diagnostic4 successfully queried18 regions, but its strict type3/exact-text-size
candidate filter admitted zero regions. No RAM data was copied. The nested
StartupRecord's default stage0 was therefore **not device evidence**.

Diagnostic5 captures the actual region table first (at most128 entries,39-bit
address space). It requires a complete, successful census before any page read.
An unmapped trailing span that extends beyond that ceiling is clipped only for
traversal; its raw metadata is retained. Out-of-range mapped regions are rejected.
Candidate RX regions may be CodeStatic3 or CodeMutable4, with no attributes,
sizes0x2000..0x100000. Exact text size is not required. At most8 candidate
fingerprints are tried; every read remains a single4096-byte mapping with
immediate cleanup. Neither kernel mapping aliases8/9 nor heap/shared-memory
regions are admitted. Page checks remain unchanged; no permission guard is
disabled to force access.

The exact SD ExeFS hash, fixed target program identity, required syscall hints,
64-byte runtime code fingerprint and36-byte GNU build note are still required
before reading the40-byte checkpoint. Fingerprints and offsets remain pinned to
startup-diag1; this is not a generic memory scanner. Region metadata is only
from our module, not unrelated processes. No target writes, patches, source
permission changes, debugger pauses or Bluetooth calls occur.

The deployed diagnostic4 binary remains untouched; this corrected reader is a
separate NACP `0.2.0-a2-diag5` app. Host regression tests cover classifications,
different region extents, each rejection flag, incomplete census, mapping
failure/cleanup, identity checks, and exclusive evidence persistence. They do
not establish the actual device mapping layout or functional module health.

## Evidence

Two new exclusive4312-byte files in `sd:/config/amazon-remote-module/`:
`layout-diag5-open.bin` and `layout-diag5-5s.bin`. No previous/partial file is
overwritten. Both are flushed/fsynced/closed and fully read back.

Layout, little endian:

- 0: magic0x41524c35/version1/entry-count/census-complete (four u32).
- 16: existing192-byte MemoryRecord, with its own checksum; see MEMORY_DIAGNOSTIC.
- 208:128 zero-initialized32-byte entries. Each has address/size (two u64),
  type/attributes/permissions/admission-flags (four u32).
- 4304: reserved0 (u32).
- 4308: FNV-1a checksum over bytes0..4307 (u32).

Entry admission flags:1 code-type allowed;2 exact RX permission;4 bounded size;
8 zero attributes;16 candidate (all previous conditions). This makes the reason
for rejection visible even when there is still no RAM checkpoint.

The report's `regions` is the number of metadata queries attempted; entry count
is the number of structurally valid replies. Zero `record_copied` means no
startup-stage claim is valid. A coherent checksum-valid checkpoint indicates the
last completed stage, not the next blocked instruction. Kernel Running can
include waiting threads. Inspect actual device evidence before changing module
initialization or trying further pairing.
