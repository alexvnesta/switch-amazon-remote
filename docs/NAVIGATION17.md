# Navigation17: full service UUID / cached client identity census

This diagnostic preserves Navigation16's navigation and operator-only Y getter
path. It does not issue a pairing request, change firmware, register a vendor
profile, replace client ownership or reset radio state. It is not a bonding fix.

The verified stored BTM executable's system pairing handler rejects a connection
when its cache does not contain a specific managed vendor service. Previously,
the remote's 128-bit UUID records were logged only as short UUID zero. Nav17
records their actual size and raw bytes, without adopting those services or
writing to them. The non-secret firmware profile UUID is used only for a
diagnostic equality comparison, never in a request or as an identity substitute.

At the initial fingerprint getter, each returned service has a bounded full
UUID log entry. One Y action at neutral READY additionally logs:

- Module instance, target handle and the cached client app-resource user ID.
- Getter result and all returned service rows, including 128-bit UUIDs.
- Malformed UUIDs, query failures, ambiguous capacity and profile-match count.
- The existing before/after exact-target checks in the context record.

The cached client ID is not the private identity stored in BTM's service object.
`cache_rows_complete=1` means the returned cache rows fit the bounded buffer and
were formatted successfully, not that physical discovery is complete. Interpret
the census only alongside a valid same-instance context record with both target
checks successful. API errors must not be converted to absence of a service.

The context record format remains168 bytes. Old Nav16 evidence is not reused as
Nav17 evidence; Nav17 uses a separate config namespace and exact module hash pin.
NPDM permissions remain identical. Staging does not launch the module, add an
Atmosphere boot flag or clear the existing link-attempt marker.

## Operator sequence — only after confirmed reboot and marker recovery

1. Open **Amazon Remote Navigation17**, press Joy-Con A once, then release all
   buttons to launch IDLE. Wait for the usual healthy-IDLE admission.
2. Press A once to scan, put the configured remote in discoverable mode, and
   press Joy-Con ZL once when Link_READY appears. Do not repeat ZL.
3. At READY / Pad_attached=1, release all remote buttons and press Joy-Con Y
   once. This captures getters, not pairing.
4. After Context SAVED, press X, wait for STOPPED / Pad_attached=0, then + to
   exit and open FTPD. Do not relaunch or clear markers yourself.

This reuses the proven link/input path, which still makes its existing bounded
GATT reads and input subscriptions. Only the *new census* is getter-only; the
whole navigation session is not a passive radio observer. Durable bonding,
automatic reconnect, wake and microphone support remain unproven.

Host ASan/UBSan tests cover real adapter control flow, malformed and unknown
UUIDs, zero/nonzero cached IDs, getter failures/capacity, no extra connection or
pairing calls, bounded logging and one-shot capture. They are not device proof.
