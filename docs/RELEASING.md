# Experimental releases

Only `vMAJOR.MINOR.PATCH-alpha.N` tags are accepted. These are developer previews,
not stable controller support. Never label a host test, build or service reply as
hardware evidence. The foreground probe remains internally versioned 0.1.10;
the release version covers the whole collection, including the 0.2.0 candidate.

## Publishing

1. Update `VERSION`, add matching `docs/releases/vVERSION.md`, and review the
   compatibility table and all hardware limitations. Commit a clean public tree.
2. Run `make privacy-check host-test release-test`. Run **Experimental release**
   manually on that commit for a build-only rehearsal; it does not publish.
3. Inspect the rehearsal artifact and its provenance. Create and push an
   annotated `vVERSION` tag at that reviewed commit. Never move a released tag.
4. Tag CI repeats Linux/macOS sanitizer and package tests, builds in the official
   devkitPro image pinned by digest, compiles the pinned public libnx source, and
   verifies all assets. Only the final publishing job has release-write access.
5. Tag CI uploads a private draft, downloads and verifies its assets, then
   publishes a GitHub **prerelease**, explicitly not Latest. A failed upload or
   verification leaves an unpublished draft for inspection. Download the public
   assets afresh and run the verifier. Do not overwrite release
   assets: fix problems in a new alpha tag with a new release note.

Action versions are pinned by commit; toolchain image and libnx source are pinned
in both the workflow and packager. Review and update both pins together. This is
repeatable build provenance, **not a promise of bit-identical compiler output**.
The packager forces a fresh build and refuses dirty source, mismatched tags,
unreviewed toolchain pins and existing output directories. The zip format itself
has deterministic ordering, permissions and source-commit timestamps.

## Assets and activation boundary

- `*-probe.zip`: foreground diagnostic NRO, notices and supervised-test docs.
- `*-module-INACTIVE.zip`: ExeFS candidate and control NRO under `candidate/`,
  deliberately outside live SD paths. No boot2 flag or target configuration.
- `*-source.zip`: complete corresponding project source, observer patch, build
  files and license texts. The pinned public libnx/SDK are separate dependencies.
- `PROVENANCE.json`: exact source commit, compiler version, dependency/image pins,
  SDK-tool and library hashes, binary/archive hashes and testing limits.
- `SHA256SUMS`: hashes of the four other download assets.

The custom passive MissionControl observer is required and **not bundled as a
binary**. Build its published patch/source using [BUILD.md](BUILD.md); stock
MissionControl alone cannot support these tools. Do not install the ExeFS module
container as a game. Do not activate, replace working files, alter pairing/radio
state or remove a safety marker just because a release is available.

## Verify a download

With the matching tagged source checkout and all five assets in a new directory:

```sh
python3 scripts/verify_release.py downloaded-assets --commit "$(git rev-parse HEAD)"
# Linux, optional additional check:
cd downloaded-assets
sha256sum -c SHA256SUMS
```

Checksums detect corruption; they are not an independent publisher signature.
The verifier checks exact inventory, SHA256, licensing, absence of activation
configuration and the inactive module paths. Do not execute downloaded code
unless you trust the published source and accept the documented experimental risk.
