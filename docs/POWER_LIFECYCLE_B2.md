# B2 power lifecycle correction

The device's B1 preflight reported an unused window permanently closed by an
early SleepReady request on two boots. No reconnect ran. The second report was
fresh and the user reported no actual sleep. The originating PSC sequence is
not known. This is not evidence of a bad bond or failed Bluetooth encryption.

B2 pauses only a never-armed lease. SleepReady, EssentialServicesSleepReady and
EssentialServicesAwake block admission. Explicit FullAwake or the
EssentialServicesAwake to MinimumAwake transition can restore availability only
after successful acknowledgement. The latter transition follows Atmosphere's
[LM handler](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/source/lm/srv/lm_ipc_server.cpp).
No wake callback launches, arms, connects, pairs, or clears saved state.

Arming, active, expired and explicitly ended leases are never revived. The B1
runtime independently refuses idle recovery if an attempt or live link exists.
Shutdown, invalid requests and failed acknowledgements fail closed. Existing
active-session cancellation remains terminal for this diagnostic milestone.
There is no automatic sleep/resume controller-support claim.

The existing metadata getter 65006 retains its 48-byte capture view and accepts
an exact 552-byte power-history view. The history contains only the last sixteen
PSC states, previous states, raw flags, results, acknowledgement results,
timestamps and admission decisions. It includes total/sequence counters and is
published with atomic words and bounded coherent-read attempts. The getter has
no key, address, radio, file or private-save access. Setup writes the history as
`power-state.bin` alongside its other bounded diagnostics. PSC callbacks do not
write files, launch threads, or issue Bluetooth requests.

Apply `missioncontrol-power-lifecycle-b2.patch` after the B1 integration patches.
The native Bluetooth patch, encryption logic, private record and input module
must remain pinned to the working B1 baseline. Off-device regression tests do
not establish the device's power sequence or saved-key reconnection success.
