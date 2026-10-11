# Experimental Amazon general-scan filter correction

This is not native BLE bonding or a production reconnect feature. Console
discovery and cleanup still require a device test. Input, target-address,
fingerprint and physical-confirmation checks remain unchanged.

`btmuStartBleScanForGeneral` takes a Nintendo profile parameter, not raw
manufacturer data. Horizon inserts `01` after the company ID, creating a
condition that differs from the captured K7Q3M7 advertisement at that byte.
Some later manufacturer bytes varied between captures and are not stable IDs.
`StartBleScanForSmartDevice` also constructs a128-bit filter regardless of
the supplied UUID size; changing only that size is not a verified workaround.

The additional MissionControl patch intercepts only the exact62-byte general
filter generated from our existing experimental parameter. It requires all of:

- The explicit `amazon-general-v1` opt-in file, including its final newline.
- A configured target and the already explicitly armed, active60-second lease.
- A request from BTM with a nonzero process ID.
- The exact expected filter index, type, condition, mask and zero padding.
- An unused correction policy for this boot.

It forwards one corrected condition through the caller's existing real-driver
session. The condition matches the six captured prefix bytes; variable trailing
bytes are excluded. That coarse hardware match is NOT a device identity check.
The existing exact-address sighting and GATT fingerprint remain mandatory.
Every other request follows MissionControl's original forwarding path.

The original BTM owner still controls starting and stopping its scan. Its exact
matching delete request is translated to the same corrected condition, including
after lease expiry, so cleanup is not attempted with the mismatching old bytes.
The correction does not allocate/delete a global slot, clear filters, initialize
the radio, create a new Bluetooth client, read another event, connect or pair.
There is no correction reset, additional scan or automatic retry. A failed
native removal retains its exact translation for another original-owner cleanup
request; the policy never generates that request itself.

The unchanged Setup app therefore continues to show STARTUP CHECK S3. Its old
manufacturer log identifies the API INPUT, not the condition actually forwarded.
The new72-byte checksummed result records add/delete API results, owner process,
times and hashes of both condition frames. It is created exclusively after the
caller ends its lease, not prematurely at expiry, and never overwrites/retries
a partial file. Sleep disables further correction and result storage for that
boot but permits exact-owner removal. API acceptance is not RF discovery proof.

Sources: [profile construction](https://switchbrew.org/wiki/Bluetooth_Driver_services#BleAdvertisePacketParameter),
[smart-device construction](https://switchbrew.org/wiki/BTM_services#StartBleScanForSmartDevice).
The stored firmware analysis supports these layouts; current live firmware
equivalence and ordinary-mode remote advertisements remain unproven.
