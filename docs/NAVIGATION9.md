# Navigation 9 supervised integration test

This is an inactive test candidate, not a public release. The foreground BLE
probe captured remote buttons, and native diagnostic8 completed a neutral HDLS
attach/update/detach lifecycle. Neither proves system navigation with this module.
Navigation9 combines those paths; app-less Bluetooth permission remains untested.

## Preparation

Keep Joy-Cons available. Physically reboot the Switch, open FTPD, and tell the
operator before running this app. The previous native process and capture lease
must be gone. The previous link-attempt lock must be preserved/recovered by the
operator only after that reboot confirmation. This app never clears the lock.

Candidate ID: `010000000000a4d3`, outside the application-ID range. No boot flag.
It uses `/config/amazon-remote-nav9` instead of the older module's config folder.
The existing target address, Nintendo blocking, firmware and pairings are untouched.

## Test

1. Open **Amazon Remote Navigation 9**. Release every controller button.
2. Press A once, then release ALL buttons for one second. The app verifies the
   exact candidate binary, successful prior native evidence and absence of the
   older native processes before a single zero-flag manual launch. Wait for IDLE.
3. Release, then press A again to arm one 60-second capture window. Put the remote
   into flashing pairing mode. Press ZL once only when `Link_READY=1`.
4. Wait for READY and `Pad_attached=1`. If either is missing, do not retry. Exit
   with + and open FTPD so the failed stage can be read.
5. On READY, exit with +, then use the Joy-Con HOME button to reach HOME. Try the
   remote D-pad, OK and Back with short presses and releases. Record whether
   the highlight actually moves; successful service replies alone are not proof.
6. Within the 90-second connected window, reopen the tool and press X to stop,
   or let the bounded session expire. Exit and reopen FTPD for evidence retrieval.

OK maps to A, Back to B, Menu to +, and Home to HOME. Media, voice and app buttons
are not implemented. Do not test sleep or wake here. A continuously held mapped
input for 10 seconds stops and neutralizes the controller. There is no automatic
reconnect and no second attempt; preserve all failure/partial evidence.

## Evidence and rollback

Launch, startup, control, status, HDLS and bounded log records are in the new
config directory. Existing module/probe files remain unchanged. No public
release, installation to auto-start, bond database reset, radio toggle or global
Bluetooth setting change is performed. A reboot clears the manually launched
candidate; do not remove prior evidence to force another attempt.
