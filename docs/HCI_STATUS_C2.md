# Received controller status diagnostic C2

This local test retains C1's saved-peer preparation and one physical-ZL
connection attempt. It adds passive evidence, not a reconnect fix. It is not
hardware-qualified until an on-console test establishes the new records.

The exact-build native tap is on the **received Command Status parser's call
site**, not the common dispatcher's entry. That common dispatcher also receives
synthetic host timeout statuses, which must not be labelled controller replies.
The tap requires an original LE Create Connection command, a 25-byte parameter
length, filter policy zero, public peer type and the exact configured peer in
wire byte order. A missing or mismatched original command produces no record.

Only status, opcode and own-address type are reported. No command payload,
keys, local addresses or unrelated peers are recorded. The privileged consumer
erases the internal event, accepts records only within its saved-peer capture
window, and retains at most four entries. A metadata-only getter allows the
input module to print `C2_HCI_COMMAND_STATUS` and a final `C2_HCI_SUMMARY`.
The window nonce is host capture scope, not a native per-request token.

Status zero means controller command acceptance. It does not prove an over-air
connection, encryption, bond retention or button input. No record is
inconclusive: it does not distinguish no submission, no response, missing
command association, relay loss or an expired capture window.

The qualified executable space is not enlarged. C2 uses the unused enrollment
capture space for this saved-only test. Explicit enrollment is refused both by
the companion and native command entry. The original key-store entry is not
hooked. Existing saved-key restoration and its live-link checks remain intact.
Native readiness includes revision2, so an old bridge fails before connecting.

C2 has a fresh evidence namespace. The prior completed C1 files and pending
marker remain preserved. After upload, reboot once, return to FTPD and confirm
the reboot before opening Setup. The operator archives the exact old attempt
marker only after that confirmation and stable readback. The launcher never
removes the marker itself. Its checks, manual start, saved-only policy and stop behavior
still apply. No automatic retry, pairing fallback, changed filter, extended
timeout, firmware update or startup activation is part of this diagnostic.
