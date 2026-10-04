# Goal: a remote that feels native to Atmosphere

The intended daily-use product is a background sysmodule, not a foreground
homebrew app. After an explicit one-time setup, it should start with Atmosphere,
recognize the configured remote, reconnect reliably and present controller input
throughout Horizon. A small settings tool can change mappings or disable support;
opening it must not become a prerequisite for everyday remote use.

The current probe/control NRO and short session limits are development tools.
They are not the finished user experience. No production/autostart mode is
provided merely by renaming those tools or removing their safety bounds.

## Implementation and admission order

1. **Background access and owned controller output.** Prove launch/PSC permissions,
   neutral virtual-controller attachment/cleanup, then real D-pad/A/B/HOME/+ input
   outside the control app, while Joy-Cons keep working. Alpha.2 makes the neutral
   check independent of BLE and exposes output/cleanup acknowledgements.
2. **Bond identity and reliable reconnection.** Establish device evidence for
   durable bond/security state and reconnect behavior. Only then implement a
   bounded target-only reconnect policy, with backoff, cancellation and explicit
   failure recovery; no blind pairing or global radio/filter resets.
3. **Idle, disconnect and real power lifecycle.** Prove cleanup, stale-input
   neutralization, actual sleep/resume and reinitialization ordering. Restore
   normal input after resume only with the preceding bond/ownership evidence.
4. **Daily-use packaging.** Build a reviewed observer dependency into a cohesive
   install/rollback package, then offer explicit opt-in autostart and persistent
   settings. Unmodified MissionControl currently lacks the required observer IPC.
   Prefer appropriate public APIs/integration over inventing another BLE stack.

Each stage needs console observations as well as host tests. A service ACK is
not evidence that a menu moved, a bond persisted or a sleeping Switch can wake.
The module and proven foreground probe must not run competing BLE sessions.

## Defaults and boundaries

Initial controller mappings are D-pad, Select/A, Back/B, Home/HOME and Menu/+.
Media, app-launch and Voice buttons currently have no default action. Optional
mapping changes should be validated and saved once, not sent through an app loop.

True remote wake is a separate hardware/BT-firmware capability: a suspended CPU
cannot run the HDLS injection loop. It is not promised by background execution.
Do not substitute permanently awake screen-off behavior silently. Microphone
audio is also separate from recognizing the Voice button and remains unsupported.

Until the preceding gates pass, releases remain experimental and inactive,
without boot2 flags, activation files, automatic pairing or reconnection. Keep
the working probe, observer, boot/recovery route, network blocking and saves intact.
