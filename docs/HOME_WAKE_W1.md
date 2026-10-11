# Home wake advertisement W1

W1 asks one question: when the Switch is asleep, does the remote broadcast
anything on a Home press that a receiver could match? It changes nothing on the
Switch. It is evidence about the remote, not a wake implementation.

## Method

A private, read-only macOS CoreBluetooth scan ran from a small ad-hoc signed
app bundle. The bundle carries its own Bluetooth usage description; a plain
command-line binary launched from an app without that key is terminated by
macOS privacy controls. The scan never connects, pairs, discovers services or
writes.

The Switch was put to sleep with its power button first. Six timed windows
with spoken prompts followed: baseline 15 s, Up 15 s, Home 15 s, Up 15 s,
Home 15 s and a quiet 10 s tail. The operator tapped once per prompt, never
held Home, and confirmed the Switch stayed asleep throughout.

Only advertisements with the K7Q3M7 manufacturer prefix, or the Mac's cached
identity for the previously observed remote, were retained. Other Amazon
products and other HID devices were counted only.

## Result, 2026-10-11

| Window | Remote advertisements | Key field | Counter |
| --- | --- | --- | --- |
| Baseline | 0 of 98 | none | none |
| Up | 56 of 175 | zero | unchanged |
| Home | 64 of 153 | nonzero, after the press | stepped by one |
| Up | 67 of 190 | nonzero, Home burst still running | unchanged |
| Home | 59 of 170 | nonzero | stepped by one |
| Tail | 10 of 70, first 3 s only | nonzero | unchanged |

Every retained advertisement was connectable, listed the HID service `1812`
and carried manufacturer data laid out as company `0x0171`, product `0x0427`,
marker `02 7d`, a two-byte key field and a one-byte counter. This matches the
broad Amazon template in the receiver source reviewed in
[receiver wake research](RECEIVER_WAKE_RESEARCH.md), and the 2026-10-03
pairing-mode samples.

- The remote is silent while idle and advertises in bursts of roughly 14 to
  17 seconds after a press. A press during a burst extends it.
- Up produced a zero key field. Home produced a fixed nonzero key field, and
  the counter stepped once per Home press only.
- A receiver filter on the full prefix plus the Home key field, with the
  counter byte masked, would separate Home from a fresh Up press.
- The Switch did not wake on either Home press with the current installation.

The exact key-field bytes and counter values are kept in the private receipt.
It is not yet known whether the key field is a remote constant or derived from
the bonded host.

## Limits

- One session with two Home and two Up presses. Only the first Up window is a
  clean Up-only sample; the second sat inside the first Home burst.
- A model-prefix or host-cache match is not physical identity. CoreBluetooth
  does not expose the MAC address.
- macOS does not report directed advertisements, so any directed traffic to
  the Switch went unobserved.
- On 2026-10-08, Up taps with the Switch **awake** produced no Mac-visible
  advertisements. Why behavior differs between an awake and a sleeping console
  is unexplained.
- Nothing here shows that the Switch's controller can hold such a filter
  through sleep, has a free filter slot, or asserts `BtWakeAp` on a match.

## What it changes

The first historical gate in [receiver wake research](RECEIVER_WAKE_RESEARCH.md),
normal-use Home advertisement evidence, is now met for this remote on one
session. The remaining gates are on the Switch side: a filter
ownership/lifetime contract that survives Horizon's sleep path, and
demonstrated receiver-to-host wake signaling. No repeat of W1 is needed unless
the bonded host or remote changes.
