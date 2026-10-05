# Navigation 11 supervised test

Navigation10 launched and registered PSC, but exited before Bluetooth setup.
The first status-file reopen returned `0xE02` (TargetLocked); a later read found
nonzero initial contents, rejected by the writer's incorrect zero-fill assumption.
There were no Bluetooth commands or HDLS output in that attempt. The system BTM
MTU/metadata path introduced in Navigation10 is still unproven on the device.

## Correction

The writer initializes the entire 88-byte checksummed snapshot after successfully
creating its own file and checking its size. It does not assume freshly allocated
bytes are zero. A temporary reopen lock retains that creation ownership for the
existing two-second bounded retry window. Once initialized, every update still
requires a valid previous record from the same instance. Any failed write latches
a permanent failure so even a caller retry cannot overwrite partial evidence.
Preexisting files, wrong sizes and corrupt subsequent records remain refused.
No file deletion, rename or truncation was added.

The candidate is isolated as `010000000000a4d5`, with
`/config/amazon-remote-nav11` and a separate control app. It has the same NPDM
permissions and stack as Navigation10, no boot flag, and no automatic Bluetooth
action. Old candidates and test files are preserved. The launcher checks that
older native candidates and this candidate are absent before its one manual launch.

Host regression tests cover nonzero and old-record-looking freshly allocated
contents, transient reopen locks, failed first writes, foreign existing files,
checksum/instance validation and admission guards. SDK compilation and these
tests are not device connection or navigation proof.

## Operator test

1. Physically reboot, open FTPD, and tell the operator before opening this app.
   The operator verifies the staged artifacts and absence of a pending link lock;
   the launcher independently checks process absence and its uptime guard.
2. Open **Amazon Remote Navigation 11**. Release all buttons, press A once, then
   release ALL buttons for one second. Wait for IDLE.
3. Press A again to arm one 60-second capture. Hold the remote's Home button until
   it flashes. Press ZL once only when `Link_READY=1`.
4. Wait for READY and `Pad_attached=1`. On failure, do not retry: exit with + and
   reopen FTPD to preserve the exact API/filesystem boundary.
5. On READY, exit with +, go to HOME with a Joy-Con, and try short remote D-pad,
   OK and Back presses/releases. Report whether the HOME highlight actually moves.
6. Within the 90-second linked session, reopen the tool and press X to stop,
   or let the session expire. Exit and reopen FTPD for evidence capture.

Keep Joy-Cons available. A continuous mapped hold for 10 seconds stops and
neutralizes the pad. No wake, microphone, automatic reconnect, firmware change,
Nintendo network-blocking change, bond deletion, NAND or save operation is tested.
