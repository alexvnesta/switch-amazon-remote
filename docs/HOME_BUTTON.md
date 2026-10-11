# Home button: supported input, separate wake boundary

The K7Q3M7 Home button already has a native Switch Home mapping. No additional
Home-enable setting is required in this project:

| Stage | Value |
| --- | --- |
| Consumer HID report 2 | `23 02 00 00`, usage page `0x0c`, usage `0x0223` |
| Decoded remote button | `Home`, bit 6 |
| HDLS output | `HiddbgNpadButton_Home`, bit 18 (`0x40000`) |
| Physical release | report 2 `00 00 00 00` clears Home |

Keyboard-report releases do not clear a held Consumer-report Home button.
Home must not be translated to the separate MissionControl report bit 12 in
the HDLS backend. Public [libnx](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/hiddbg.h)
defines the HDLS Home bit. `flags=1` is the controller's power flag, not a
Home enable switch. The module does not modify global Home-button protection,
activate AM-owned Home shared memory or synthesize applet system actions.

Earlier physical captures include this exact Home payload, decoded Home,
successful bit-18 output and its release. That establishes the report path,
not acceptance at every system screen. A6 retains it unchanged.

## Three different acceptance cases

1. **Awake Home navigation:** briefly pressing remote Home should emit one
   genuine Home down/up sequence after Bluetooth and controller setup succeed.
2. **Cold-boot or manually awakened entrance screen:** the user has now
   reported dismissing the first-boot screen with three Up presses, supported
   by acknowledged direction input in session 7552. This does not verify Home
   specifically or every boot/wake transition. The module has no frontend or
   unlock-screen predicate. Cold boot has an initial operating state;
   subsequent power transitions require acknowledged PSC Awake/state 0.
   Public PSC documentation does not establish that state's timing relative
   to the entrance screen. Connection latency also remains relevant.
3. **Waking the sleeping console:** decoded Home output does not establish
   this capability. The current implementation requires manual wake. Upstream
   [MissionControl's wake FAQ](https://github.com/ndeadly/MissionControl#frequently-asked-questions)
   likewise distinguishes the hardware wake mechanism from normal input.

Nintendo's optional post-wake screen lock uses three separate presses of the
same button. This is separate from a Home prompt or controller-pairing screen.
See [Nintendo's screen-lock instructions](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22371/~/how-to-enable/disable-screen-lock-on-nintendo-switch).
Use three deliberate Select/A presses if prompted. There is no automatic
triple-press macro and no automatic lock-setting change.

## Short physical validation when the operator returns

First establish the awake case: once the remote navigates, open an ordinary
application and **briefly press and release remote Home**. Observe whether it
returns to HOME. A long hold can have different Switch or remote behavior;
do not hold it into the remote's pairing mode.

Then test cold boot without first dismissing the entrance screen using a
Joy-Con. Tap and release remote Home every two seconds for up to 45 seconds.
Record the displayed prompt and whether Home dismisses it. If a three-press
screen follows, try three separate Select presses.

Test sleep separately, only after a successful connection: sleep manually,
wake with the console power button, and remain at the entrance screen while
trying brief Home taps. This does not test waking via the remote.

If a screen does not respond, use the Joy-Con to reach FTPD and report exactly
which screen remained. Preserve logs; do not re-pair or repeat unexplained
reboots. The triage must correlate power ACK, ARM, connection/security ready,
Home down/up output and visible response. A successful output ACK alone is
not an unlock-success claim.

## Regression coverage

The real decoder and real HDLS adapter are replayed together against SDK
fakes for Home press/release, duplicate reports, independent keyboard and
Consumer reports, three replayed tap sequences, Select/A taps and held-Home cleanup.
The actual transport adapter separately exercises Consumer release before
Close. Startup tests compose the production power, health and auto-start
classes without frontend input. None of these tests simulate the Horizon
entrance UI or prove hardware sleep wake.

The test SDK now matches the real FullKey15 and USB enum values, 15 and 3.
Compile-time assertions check those values with the actual SDK. This fixes
a test-model mismatch, not an asserted runtime controller-type defect.

## True sleep-wake research, 2026-10-09

Remote Home waking the console is an open requirement. It is not satisfied
by the awake-input tests above, and it should not be dismissed as inherently
impossible just because a sysmodule cannot process input while suspended.
Hardware configured before suspend can be responsible for waking its host.

There is now substantive original-Switch wake research:
[NS1-Wakeup-Demo, revision b3f9b7f](https://github.com/608/NS1-Wakeup-Demo/tree/b3f9b7f45a6cf29360899135ee6e96454574a3d9)
implements a dedicated radio exchange before normal BR/EDR reconnection.
The author reports an ESP32 revision 3 implementation, requiring controlled
radio firmware and prior Switch pairing. Its Windows input bridge explicitly
handles disconnected Home separately from ordinary input. This is a credible
external-bridge lead, not a tested Amazon integration or a drop-in sysmodule.
Its code and documentation use CC BY-NC 4.0; none was imported here.
Switch 2 BLE wake advertisements are a separate mechanism.

An Amazon bridge would still need an always-powered receiver that can obtain
the remote's actual Home event, plus verified coexistence or handoff between
remote-facing and Switch-facing links. Seeing a reconnect advertisement alone
does not establish that Home, rather than another button, caused it. Pairing
the remote to a new receiver must be an explicit operator decision.

There is also a possible receiver-side research direction.
[Infineon's WakeOnLE example](https://github.com/Infineon/linux-example-btstack-wakeonle)
uses a powered Bluetooth controller to filter advertisements and assert a
host-wake signal. Its supported-chip list does not include CYW4356 and it
depends on specific controller firmware, host software and GPIO integration.
It establishes general feasibility, not support in the original Switch's
Horizon firmware. Public [`btmAcquireAwakeReqEvent`](https://github.com/switchbrew/libnx/blob/master/nx/source/services/btm.c)
returns an event handle;
it does not provide a peer/report wake-enable interface.

Our completed-session sleep path neutralizes/detaches the virtual controller,
retires local notification paths and stops input polling. It does not explicitly
unpair, switch the radio off, or command a BLE disconnect in that path. The
committed bond remains saved. Proprietary HOS service-close side effects are
unknown, so this does not prove the on-air connection survives suspend. Simply
keeping services open would not establish a wake mechanism.

The [HID Information H1 diagnostic](HID_INFORMATION_H1.md) has now physically
read characteristic `0x2A4A` during a successful encrypted connection. Session
7552 returned `00 01 00 00`: HID version 1.00, country 0, `RemoteWake=0` and
`NormallyConnectable=0`. These are returned value bytes, not characteristic
declaration properties. This does not exclude a proprietary wake mechanism.
The user also reported dismissing the first-boot screen with three Up presses;
the corresponding direction input was acknowledged. No Home report occurred
in that session, so this is not a Home-specific entrance or sleep-wake test.

The [receiver-side research](RECEIVER_WAKE_RESEARCH.md) now connects public
Amazon WoBLE code, the Switch's documented host-wake signal, and verified
command builders in a stored Horizon Bluetooth image. A bounded offline replay
of a selected BTM sleep path now shows owned BLE scan stopping, tracked GATT
disconnect requests and LLR scan selection before sleep acknowledgement.
The lower driver path also prepares controller-resident Classic link keys
before a native vendor-mode request and changes the host-to-radio GPIO output.
That is not a read-only BLE wake probe and must not be invoked experimentally
on the working setup. Neither replay establishes the live branch or radio
response. Radio capability,
complete suspend/filter behavior and normal Home-triggered advertisements
remain unproven. No wake-specific radio or firmware changes have been deployed.

Before any wake-specific deployment, establish either a supported Horizon
receiver-offload mechanism and its lifetime requirements, or an explicitly
approved external bridge. Keep normal Home input, entrance-screen acceptance,
and true remote-triggered sleep wake as separate hardware acceptance tests.
Do not replace those tests with a pairing-mode hold, screen dimming, manual
wake, a successful HID output acknowledgement or an untested firmware change.
