# Isolated capability diagnostic Q2

**Withdrawn from testing after the 2026-10-09 physical trial. Do not run Q2.**
Pressing A was followed by an instruction abort in the Bluetooth service and
a companion abort after its driver IPC session closed. No Q2 report or
controller capability reply was recovered. Static inspection subsequently
identified a calling-convention mismatch in the reused legacy command path,
described below. This is not evidence of unsupported wake, and the successful host
tests did not cover this failure. The paused automatic-start setting was
restored and verified for the next user reboot. The instructions below are
historical, not authorization for another trial.

## Failure diagnosis and withdrawal

The retained image and the exact installed patch were hash-checked before
inspection. The specialized route used by saved-peer navigation establishes
the native stack frame. The old generic custom-command route used by Q1/Q2
does not establish that frame, but returns through the native epilogue that
expects it. This is a concrete ABI defect consistent with the observed invalid
instruction target, followed by the companion's closed-session abort. No
dynamic crash reproduction or radio command was performed during the audit.
Whether the capability command reached the controller remains unknown.

An earlier metadata audit had identified the legacy-route mismatch and isolated
the newer commands from it. Q1 incorrectly reused the legacy route without
carrying that restriction into its transport review. The fake-SDK tests covered
the caller state machine, not this native entry/return contract. Adding a caller
timeout or suppressing the companion abort would not repair the driver fault.

The source now contains a no-transmission runtime, and the foreground program
only displays a withdrawal notice. The runtime regression links without any
Bluetooth transport or worker stub and refuses every legacy opcode. Both old
staging entry points reject deployment before filesystem/FTP actions; Q2's
separate enable-file restoration remains available. Historical decoder and
state-machine tests remain useful offline, not as deployment approval.

Recovery uses the preserved pre-Q1 companion and quarantines the diagnostic
NRO by changing its launchable extension. It does not repair the legacy driver
bridge itself. The normal input module, driver patch, pairing storage, boot
flags and Nintendo network-blocking configuration must remain unchanged, with
their file hashes verified. A future capability acquisition path needs a new
end-to-end transport review before any device test.

Q1's first physical trial stopped before sending the capability query. Automatic
remote reconnection had already timed out; its cleanup could not confirm the
owned link was absent. Q1 correctly rejected that nonzero result. No controller
capability reply was obtained, and that trial says nothing about wake support.

Q2 isolates the same read-only query from the automatic input module. The
companion and its query transport, input-module binary, Bluetooth driver patch,
boot flags, saved bond and network blocking are unchanged. Only the foreground
diagnostic and a reversible enable-file pause are required.

## Historical test, not cleared for repetition

1. Reboot after Q2 is staged. Use the Joy-Cons; automatic Amazon remote control
   is temporarily paused.
2. Open **Amazon Remote Capabilities**. Check that its heading says **Q2**.
3. Press Joy-Con **A once**, keep the console awake and leave the remote alone.
4. Wait for either **CAPABILITIES RECEIVED - REPORT SAVED** or
   **CHECK BLOCKED/FAILED - REPORT SAVED**. Press **+** and open FTPD.

Do not open Amazon Remote first, pair, or retry. A saved report can contain a
failure. It does not establish deep-sleep wake. A save failure also calls for
returning to FTPD rather than repeating the query.

The operator managing staging must restore the verified paused enable file
after collecting the report or abandoning this test. This pause does **not**
automatically expire after one boot. Normal remote startup resumes on a later
reboot after restoration; Q2 never launches the input module or reboots.

## Admission and evidence

The staging record names the pre-test input-module instance. Q2 requires a
different, checksummed startup record at the existing disabled-enable exit,
the live opt-in file absent, and its preserved copy byte-identical to the
expected opt-in. The pinned input module exits at that point before PSC or
Bluetooth initialization. Q2 also requires the native process lookup to report
that input-module program absent; other lookup errors do not qualify.

The companion capture must be unused, with no prior close or active deadline.
Only then does Q2 close that passive capture through the existing metadata-only
EndTargetCapture method. It verifies the explicit-close reply and rechecks boot,
isolation files and process absence before using the unchanged Q1 query. This
does not reset a consumed lease, cancel a connection, or ignore an old error.

The Q2 report has a separate filename and schema, preserves the two capture
snapshots and preparation stage, and uses exclusive creation, a checksum and
full readback. Q1's report remains untouched. A matching companion response is
still required. The Q1 limits on underlying IPC timing and interpretation of
filter capabilities remain in effect.

## Historical validation and its missing coverage

Host tests exercise a successful isolated preparation and refusals for old boot
identity, incorrect disabled checkpoint, missing isolation, process presence,
non-not-found process errors, used/expired/closed leases, stale or malformed
metadata, failed close and changing isolation/process state. Every byte of the
persisted report is covered by corruption tests. The real frontend is built
against the Switch SDK. These tests are not device capability or wake proof.
They also did not validate the legacy native wrapper's calling convention.
