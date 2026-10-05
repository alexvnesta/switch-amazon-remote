# Navigation13: owned virtual-controller lifetime

This is an inactive, supervised candidate, not a production release.
Navigation12 received and decoded an actual Down notification in a native
module. Both input subscriptions completed and its virtual controller accepted
the initial neutral state. Approximately 53 seconds later, the first mapped
update returned `0x1C24CA`; cleanup Set and Detach returned the same error.
Those facts prove native notification delivery, not durable bonding, visible
HOME navigation, or the exact meaning/cause of the HID error.

## Narrow change

Bluetooth discovery, fingerprint, security requests, subscriptions, input
decoding, session deadline and the 15-second stable-IDLE gate are unchanged.
Navigation13 checks virtual-controller presence every 250ms while READY and
immediately before a changed mapped input. The read-only SDK snapshot is bounds
checked (0..16 entries) and searched only for the handle returned by our own
Attach call. Foreign controllers are never adopted, logged, reassigned or
modified; no ApplyHdlsStateList or assignment commands are used.

If and only if a successful query confirms our handle is absent, one replacement
may be created in our existing work session. Its presence is checked and a
neutral state is sent before any mapped input. A mapped-button release must
then be observed; the press/hold which discovered removal is never replayed.
A second disappearance, invalid snapshot, query failure, attach failure or
ambiguous Set error stops the session. There is no reattach loop, Bluetooth
reconnect, bond deletion, radio/filter change or other-controller fallback.

Each own-handle check and delivery failure is logged. The text log closes after
terminal stop/cleanup, allowing FTP retrieval without a diagnostic-only reboot.
The one-shot native process may remain resident in STOPPED; closing the UI does
not restart it. Do not relaunch/retry after a failure.

The removal hypothesis is supported by public controller implementations, not
yet proved on this device. In particular, if our handle remains present but Set
still fails, this candidate stops and records that distinction rather than
pretending it fixed the error.

## Public references and local acceptance

The public [sys-con HDLS handler](https://github.com/cathery/sys-con/blob/e79be831614952aa84f7feb48cea5b6329ff07dd/source/ControllerSwitch/SwitchHDLHandler.cpp)
checks for erased virtual devices before sending state. Our implementation is
independent and deliberately more bounded: at most one replacement, confirmed
absence only, neutral/release gate, no retry after an ambiguous result.
The [libnx snapshot API](https://github.com/switchbrew/libnx/blob/feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973/nx/source/services/hiddbg.c)
reads through our existing session/work buffer. No private MissionControl BLE
source was used. Firmware/program permissions are unchanged from Navigation12.

Host tests exercise long idle gaps, removal between check and input, held keys,
release gating, second disappearance, recovery failures, clock rollback and the
captured Set error without recovery. The actual HDLS adapter tests reject query
errors, negative/oversized counts and duplicate own handles, and ignore foreign
handles. ASAN/UBSAN and real SDK warnings-as-errors builds must pass. These
models do not prove controller lifetime or visible navigation on Horizon.

## One supervised test

1. The operator preserves Navigation12 evidence and archives ONLY its pending
   attempt marker after Alex's confirmed physical reboot. No old files, bonds,
   boot flags, firmware or saves are replaced. The Navigation13 launcher also
   requires all prior/new diagnostic native processes to be absent and verifies
   the exact candidate bytes. If a fresh reboot has already been confirmed and
   no tool was opened afterward, an additional reboot is not needed.
2. Open **Amazon Remote Navigation13**; A once, release all buttons. Leave the
   remote untouched until **IDLE health PASSED** (at least 15 seconds).
3. A to arm; make the remote flash; ZL once only when **Link_READY=1**.
4. On **READY / Pad_attached=1**, release all remote buttons, then test one short
   Down press/release. Exit the UI with +, use Joy-Con HOME and test short
   D-pad/OK/Back presses. Report actual visible highlight movement.
5. If a pad recovery occurs, release all mapped buttons before pressing again.
   If any step fails, do not retry. Exit the UI and reopen FTPD for logs.
6. Within the bounded 90-second linked session reopen the tool and press X to
   stop, or let the deadline expire; then return to FTPD. Keep Joy-Cons available.

Persistent bond/reconnection, sleep/wake, microphone and production auto-start
remain out of scope and unproven.
