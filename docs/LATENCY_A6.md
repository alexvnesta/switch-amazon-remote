# Compact diagnostics A6 preview

A6 retains A5's connection, saved-key security, retry, controller, Stop and
sleep/wake logic. It omits redundant raw HID metadata and per-button dumps
from the input module's synchronous SD log. It retains GATT event headers,
all API results, fingerprint/security decisions, output ACKs and cleanup.
Inventory queries, validation and subscriptions are unchanged.

Every actual log write is timed in memory. Cumulative totals and the slowest
write are reported after attachment and Stop. Accepted notifications carry
host-dequeue and post-decode timestamps into the existing output-ACK record.
These measurements exclude the physical button press and over-the-air delay.
Rejected notifications and setup reads never become accepted-input samples.
The diagnostic counters do not control radio, security or input decisions.

The preceding A5 test took 20.935 seconds from accepted connection request to
the target callback, then about 3.7 seconds to input ready. The trace has no
physical button or advertising-start timestamp. A6 does not claim to fix that
pre-link delay. It reduces known logging overhead without guessing at global
scan parameters, changing keys or interrupting a pending connection.

Test: reboot, remain on HOME and tap remote Up every two seconds for up to
45 seconds, starting as soon as HOME appears. Do not hold Home or re-pair.
Check navigation, then open Amazon Remote, X Stop, wait for STOPPED, +, FTPD.
Record approximately when Up first starts moving the selection. Sleep/wake
is a separate test, not a way to rescue this startup measurement.
