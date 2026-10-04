# Build and public-source provenance

## Portable verification

No Switch SDK or device connection is needed for:

```sh
make host-test
python3 scripts/check_public_tree.py
```

The first command runs12 ASAN/UBSAN targets, including29 scenarios against the
actual libnx transport through a fake SDK, plus the module's65,536 mapping masks
and resource/lifecycle/protocol tests. A separate module test compiles the actual
HDLS adapter against a minimal fake SDK; that is not an ABI or permission proof.
Linux/macOS CI runs those same targets.
Optional TSAN: `make -C host_tests tsan-capture-lease` (runtime support varies).

## Switch probe and inactive module candidate

Install devkitPro/devkitA64, libnx and the Switch tools using their official
installation instructions. Set `DEVKITPRO`; do not point it at this repository.
Known local build provenance:

- devkitA64 GCC16.1.0.
- Public libnx commit `feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973`.
- No global libnx ABI change was made; layout assumptions are compile-time checked.

```sh
export DEVKITPRO=/opt/devkitpro
make switch-build
```

Outputs: `tools/amazon-remote-probe/amazon-remote-probe.nro`,
`tools/amazon-remote-module/build/exefs.nsp`, and
`tools/amazon-remote-module/build/amazon-remote-control.nro`.
Build outputs are ignored by Git. Experimental prereleases can publish only the
reviewed probe and inactive candidate packages, with source and provenance; SDK
archives and private receipts are never published. A successful build is not a
device-compatibility proof. See [release policy](RELEASING.md).

## Required passive MissionControl observer

The current HID16 advertisement path requires our passive observer bridge.
The patch targets public MissionControl commit
`71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e` (the local public0.16 base).
Related dependency pins from that reviewed build:

- Atmosphere-libs `aae2da0155a490abd3bfd17a70ccd16bb73c916a`.
- libnx `feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973`.

To prepare source in a fresh directory, from this repository root:

```sh
git clone https://github.com/ndeadly/MissionControl.git ../MissionControl-observer
git -C ../MissionControl-observer checkout 71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e
git -C ../MissionControl-observer submodule update --init --recursive
git -C ../MissionControl-observer apply --check "$PWD/patches/missioncontrol-observer.patch"
git -C ../MissionControl-observer apply "$PWD/patches/missioncontrol-observer.patch"
cp -R mc_mitm/source/amazon_remote ../MissionControl-observer/mc_mitm/source/
```

Inspect the resulting diff and follow upstream's complete toolchain/dependency
build instructions. The observer patch/source is published, but rebuilding this
dependency is **not yet covered by this repository's automated CI**. No claim of
bit-reproducible observer output on arbitrary SDK versions is made.

Configure only your own exact target in `sd:/config/amazon-remote/address.txt`.
Observer configuration uses `observer-mode.txt` with `passive` and
`target-capture-mode.txt` with `diagnostic-device-zero`, in that same directory.
Review `managed_observer_runtime.cpp` before installation. These files enable
diagnostics for the explicit target; they are not boot-time pairing instructions.
Intent tracing is separately opt-in and can capture identifying metadata.

Use only a supervised install with backups. Do not replace a working module or
enable boot2 just by copying an upstream distribution package. See TEST_PLAN.

## Licenses and references

Use the official [devkitPro instructions](https://devkitpro.org/wiki/Getting_Started),
[libnx source](https://github.com/switchbrew/libnx), and
[public MissionControl source](https://github.com/ndeadly/MissionControl).
Retain upstream notices and this repository's GPL/MIT/ISC license texts.
