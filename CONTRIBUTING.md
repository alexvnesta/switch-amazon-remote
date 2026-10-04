# Contributing

Run `make host-test` and `python3 scripts/check_public_tree.py` before submitting
a pull request. Keep patches narrow and distinguish tested hardware results from
host tests, successful IPC and proposed behavior.

For a new remote, provide model number, firmware version, public VID/PID and
sanitized descriptor/report metadata. Do not assume an advertised name or MAC
address proves identity. Maintain bounded inventory parsing and explicit
ReportReference/CCC matching; do not guess notification properties or report IDs.

Never post Bluetooth addresses, pairing keys, console/account identifiers, full
crash dumps, NAND data, Wi-Fi credentials or other people's captures in issues.
Local diagnostic logs can include these fields; review and redact before sharing.
Private vendor source is not needed and should not be submitted.

No auto-pair/reconnect storm, global driver initialization, broad controller-list
replacement or silent change to working recovery/network-blocking state. New
power behavior must have a supervised device test and a rollback plan. Do not
claim support for wake or microphone audio from a decoded button press.
