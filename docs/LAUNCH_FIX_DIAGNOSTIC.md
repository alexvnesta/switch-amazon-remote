# Supervised launch correction: diag6

This is a local diagnostic, not a BLE release. Do not reboot or pair a remote.
Open **Amazon Remote Launch Fix**, release the buttons, press **A once**, then
release **all buttons**. Wait 10 seconds. Press **+**, then open FTPD.
If refused or saving fails, photograph the screen and return to FTPD; do not retry.

The prior R-button launch returned a PID but its executable layout matched the
installed Homebrew Loader rather than this module. That is a hypothesis, not yet
confirmed by PM's saved override flags. This tool first queries those flags with
Atmosphere command 65000, a copied process handle, and exact program identity.
It never attaches a debugger, reads unrelated memory, or terminates a process.

Launch is allowed only if the prior matched process reports `IsHbl` (flag bit 0),
the new fixed ID is explicitly absent (`0x20f`, PID 0), and the exact candidate
size and SHA256 match. A fresh A-only press after neutral input arms one attempt.
All buttons must then remain released for one second. The exclusive intent is
flushed, synced and read back before the PM launch. Any prior/partial intent or
temporary receipt blocks another launch. Failed getters are not absence proof.

The candidate is `010000000000a4d0`, outside NCM's application ID range. Its main
NSO and permissions are unchanged; only the three NPDM program-ID fields differ.
It has **no boot2 flag**. The old module, boot flag and global HBL configuration
are untouched. The PM request uses storage None and zero launch flags; no debug
hook, start-suspended, ASLR adjustment, Bluetooth, pairing or controller injection.

Evidence paths under `/config/amazon-remote-module`:

- `override-diag6.bin`: 80-byte exclusive open-time legacy override snapshot.
- `launch-fix6.bin`: 336-byte intent/result/post-launch snapshot, FNV checksummed.
- `launch-fix6.tmp`: owned publication temporary; preserve if left behind.

The fixed receipt contains the legacy/before/after override snapshots, exact
Results, returned PID, timestamps and pinned candidate digest. The five-second
getter records the candidate's saved override flags. PM acceptance alone is not
module health. Startup checkpoints and fresh status still need device review.
This manual-launch finding also does not by itself explain earlier boot2 failure.

Host sanitizer tests exercise policy, adapter calls and persistence. The SDK
build verifies compilation, not hardware ABI/permission or functional startup.
No public release is made until device evidence supports it.

Rollback: archive only the new app and inactive candidate after reviewing the
evidence. Do not clear attempts or touch the old module/boot flag automatically.
