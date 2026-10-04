# Alpha.2 launch diagnostic2 (supervised local tool)

Separate **Amazon Remote Launch Check**, NACP0.2.0-a2-diag2. This is not a public
release or pairing app. It leaves the deployed startup-diag1 module, previous
control app, NPDM/permissions and activation unchanged. No reboot is needed to run
this launch test after the reviewed FTP transfer.

## Operator test

1. Exit FTPD and open **Amazon Remote Launch Check** from Homebrew Menu.
2. Release the buttons. Press **R once** (the right shoulder, not ZR).
3. Leave the screen open for10seconds, press + to exit, then open FTPD.
4. Do not pair the remote, press other test buttons, or reboot during this test.

Only one deliberate R edge is eligible. There is no launch on app open, no
automatic retry, and no launch of an existing process. The process lookup must
return exactly PM ProcessNotFound0x20f; access/getter errors are not absence.

The live module file must be exactly142150bytes and SHA256
`984a813aa00416a87dc38be88de017c27b859a1afbd2822ef88a9290089e1580`.
This pins the reviewed Bluetooth-disabled startup-diag1 binary. It is not a
general-purpose sysmodule launcher; any rebuilt/different binary is refused.
Launch uses only program ID0100000000a4d001, StorageIdNone and zero launch flags,
matching boot2's custom SD module request. No debug hooks, start-suspended flags,
ASLR changes, kill/terminate APIs, controller commands or Bluetooth APIs exist in
this tool. Hash verification assumes no concurrent replacement of the SD file;
it is not an adversarial secure boot implementation.

## Evidence and stopping boundary

Before requesting launch, the app exclusively creates, flushes, fsyncs, closes,
and reads back `sd:/config/amazon-remote-module/launch-diag2.bin` (112bytes).
Any existing or partial attempt file refuses further launch, even after reopening
the UI. Preserve this marker until a reviewer has inspected the result. The
pending-link marker is unrelated and remains untouched.

The receipt records the raw PM getter, pm:shell init, launch Result and returned
PID. Stage1 is saved intent, not a launch result. Stage2 is service-init failure;
stage3 means the launch API returned, including an error. A second bounded update
adds a fresh process lookup after approximately5seconds. A successful launch
reply/PID is not proof the module stays alive or produces working input.

Updated receipts use the existing complete-temp publication helper. This is not
crash-atomic replacement. Existing temp files are preserved and block update; if
final saving fails, photograph the displayed Results and retrieve both `.bin` and
`.tmp` rather than retrying a launch or deleting evidence.

Retrieve the receipt, startup-diag1 checkpoint, module.log and status.bin over FTP.
No extra attempts are needed until those Results have been reviewed. Host policy
and fake-SDK adapter tests plus a real SDK build are not hardware validation.
