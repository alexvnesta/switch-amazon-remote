# Saved discovery B3

The B2 device test loaded the private saved bond after reboot, but its
address-only connection attempt obtained no handle. Native stored-key encryption
was never reached. This does not demonstrate a bad bond or a need to re-pair.

B3 removes the central-bond branch's no-scan shortcut. It reuses the existing
exact manufacturer scan and managed target-sighting gate, including the P3
three-second admission age and 250 ms dispatch budget. Scanning never connects
automatically. One physical ZL decision is still required after a fresh sighting.
The separate known-address experiment cannot be compiled with central-bond mode.

This particular build requires Saved metadata at initialization and again before
the native security request. Empty, unknown, malformed or failed metadata stops
the test. It does not fall back to first enrollment. The existing observer loads
its private record once and owns subsequent security processing; B3 changes no
observer code, private storage, native patches, controller slots or cleanup route.

Setup distinguishes SEARCHING SAVED REMOTE, REMOTE SEEN, CONNECTING TO REMOTE
and SECURING SAVED LINK. Errors and stop/power states retain priority. Normal-mode
advertising and saved-key reconnection remain hardware-unverified. If no remote
is seen, stop and preserve the diagnostic rather than entering pairing mode.

Host tests compile the actual transport with both central-bond and saved-discovery
flags. They cover saved-mode scan admission, stale/invalid sightings, pending
markers, dispatch delay, pre-existing target refusal, no automatic connection,
missing/malformed store metadata, metadata loss before security, a connection
request without a handle, one security request after link and fingerprint checks,
security completion/error binding, and UI state precedence. These are simulated
tests, not evidence of successful on-device reconnection.

Deployment replaces only the input module, Setup and its note. Preserve the
currently running session and pending marker until a user-confirmed reboot.
After reboot, prepare the next session using exact archived diagnostics. Do not
erase or read private key material, reset the remote, or enable input autostart.
