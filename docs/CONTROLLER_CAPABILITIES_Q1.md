# Controller capability diagnostic Q1

**Withdrawn from testing:** the first physical Q1 trial was blocked before
the query by the automatic connection's unconfirmed cleanup. The isolated
[Q2 trial](CONTROLLER_CAPABILITIES_Q2.md) subsequently crashed the Bluetooth
service using the same query transport. Do not run either query or repeat the
steps below. Static inspection now confirms a native stack-frame mismatch in
the reused legacy command path. The query runtime and foreground source are
disabled, and the old staging scripts reject deployment. A replacement would
require a new transport review, not merely another caller timeout.

This supervised diagnostic asks the Bluetooth controller for its Android vendor
capabilities with one fixed `LE_Get_Vendor_Capabilities` (`0xfd53`) command and
zero parameters. It is an **active read-only query**, not passive startup capture
and not a sleep-wake implementation. It does not request controller identity.

## Historical physical test, not cleared for repetition

1. Reboot after the companion and diagnostic app have been staged.
2. Open **Amazon Remote**, press Joy-Con **X**, and wait until **STOPPED** or
   **DISCONNECTED** is confirmed. Press **+** to exit.
3. Open **Amazon Remote Capabilities** and press Joy-Con **A once**. Keep the
   Switch awake and leave the remote alone.
4. Wait for **REPORT SAVED AND VERIFIED**, then press **+** and open FTPD.
   A failed check also produces a report. Do not repeat or re-pair.

The app retains a checksummed, exclusive-create, readback-verified report under
its own app directory. Existing reports are never overwritten. Green means the
report was saved, not that wake is supported. A save error should be reported
without retrying. The regular Amazon Remote app remains installed.

## Scope and failure behavior

- The existing reviewed driver patch, input module, enrollment, keys, filters,
  sleep mode and boot configuration are not changed.
- The companion permits a single query per process lifetime. There is no retry
  button and no arbitrary command input. Current stopped-session evidence and
  an ended capture lease are required. The supervised build admits only the
  selected Horizon 22.5.0 firmware version.
- The existing MissionControl vendor-command bridge is used. A dedicated worker
  isolates that synchronous driver call from the companion's service loop. Its
  static stack is not reclaimed or reused if a driver call remains pending.
- Status has a 12-second deadline and the frontend has a 14-second polling
  deadline. These are not a proof that every underlying driver/IPC operation
  has a global wall-clock bound. No process termination is used to cancel it.
- PSC transitions interrupt the query. A new submission is not admitted during
  an unacknowledged power transition. An already submitted controller command
  cannot be recalled. Do not sleep the console during the test.
- Only an owned `0xfd53` custom reply is consumed before MissionControl's generic
  custom-response wait. Unrelated event routing is retained. Late matching
  replies are drained without reading their payload. The opcode remains
  reserved for the rest of that boot so a late reply cannot become a retry's
  result. Failed/unfinished issued queries block generic custom commands until
  reboot; normal input-event routing is not replaced.
- The integration patch is applied to the previously reviewed wake-capable
  companion, with the four controller capability/query source files overlaid.
  It is not a patch against arbitrary upstream MissionControl releases.

Returned filtering support and capacity do not prove unused filter slots,
filters enabled during sleep, controller firmware identity, the remote's Home
advertisement pattern, or deep-sleep wake. Device evidence remains required.

## Validation

Host tests cover reply-before-submission and submission-before-reply ordering,
malformed envelopes, single-use admission, timeouts, late replies, power changes,
thread-creation and driver errors. Runtime tests compile the real runtime body
against a fake SDK and check that the mutex is not held across the driver call.
Frontend tests check current-boot/stopped-state admission, report freshness and
all-byte checksum corruption. SDK builds verify both the companion and NRO.
These tests are not controller hardware or end-to-end wake proof.
