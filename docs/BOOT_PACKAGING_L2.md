# Supplemental-image packaging qualification

The L1 startup experiment was withdrawn after console boot hangs. Container
hashes and loader-layout checks were insufficient: its supplemental executable
began with ordinary function instructions rather than native runtime metadata.
The native runtime enumerates such executable images before argument parsing.

L2 isolates the packaging correction. It adds an inert module with:

- A canonical legacy entry/header-location prefix and a mapped MOD0 header.
- A bounded dynamic table with no dependencies, callbacks or relocations.
- An empty symbol table and a nonzero-count empty hash table.
- A dedicated writable page for the runtime module object, not shared storage.

It does not introduce the proposed capability-observation hooks. Existing remote
input and companion modules remain unchanged. This is not a deep-sleep wake fix.

The public source-owned module is in
`tools/driver-capability-p1/runtime_probe.S` and `runtime_probe.ld`.
The foreground getter app is `boot_layout_l2_main.cpp`. It reads process and
loader module metadata once and saves an immutable report. It does not pair,
scan, read keys, launch processes or alter radio configuration.

Host tests, SDK container cross-checks and exact-build static review qualify a
controlled startup test, not successful hardware operation. The retained main
and combined package contain private firmware and must never be published.

## Test and recovery boundary

The first physical test is normal boot followed by **Amazon Remote Boot Check
L2**, Joy-Con A once, then + and FTPD. There is no remote pairing step.

If boot hangs, enter Hekate SD USB mounting and withdraw only the newly added
native override into its recorded transaction quarantine. Keep the original
patches, remote modules, OC configuration and recovery artifacts intact.

The deployment is no-overwrite and full-readback checked. Interrupted staging
is preserved, not deleted or retried blindly. The failed L1 package is blocked.

## References

Public runtime metadata definitions and examples:

- [Atmosphere runtime header definitions](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/rocrt/rocrt.hpp)
- [libnx CRT metadata](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/switch_crt0.s)
- [Skyline module-object definition](https://github.com/skyline-dev/skyline/blob/master/include/ModuleObject.hpp)

These references are corroboration. The selected firmware's exact startup
behavior, object bounds and legacy-header acceptance were checked separately
without executing firmware. A homebrew MOD0 header alone is not enough.
