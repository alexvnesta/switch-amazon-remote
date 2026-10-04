#!/usr/bin/env python3
"""Build a clean public revision and package only explicitly allowed artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LIBNX_COMMIT = "feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973"
IMAGE = "devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282"
MODULE_ID = "0100000000a4d001"


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def validate_version(version):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+-alpha\.[1-9][0-9]*", version):
        raise ValueError("Only explicitly experimental alpha versions may be released")


def archive(path, entries, epoch):
    """Stable archive metadata; no host paths, modes, timestamps or symlinks."""
    for name in entries:
        if name.startswith("/") or ".." in Path(name).parts or "\\" in name:
            raise ValueError("Unsafe archive path")
        if Path(name).name in {"boot2.flag", "enable.txt", "address.txt"}:
            raise ValueError("Activation or identifying configuration is forbidden")
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, time.gmtime(max(epoch, 315532800))[:6])
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            out.writestr(info, data, compresslevel=9)
    with zipfile.ZipFile(path) as check:
        if check.testzip() is not None or set(check.namelist()) != set(entries):
            raise ValueError("Archive verification failed")
        for name, data in entries.items():
            if check.read(name) != data:
                raise ValueError("Archive content mismatch")


def package(output, image, libnx_commit):
    version = (ROOT / "VERSION").read_text().strip()
    validate_version(version)
    if image != IMAGE or libnx_commit != LIBNX_COMMIT:
        raise ValueError("Release toolchain image and libnx source must match reviewed pins")
    if command("git", "status", "--porcelain"):
        raise ValueError("Refusing release from a dirty or untracked source tree")
    commit = command("git", "rev-parse", "HEAD")
    if os.environ.get("GITHUB_REF_TYPE") == "tag":
        if os.environ.get("GITHUB_REF_NAME") != "v" + version:
            raise ValueError("Tag does not match VERSION")
    subprocess.run(["python3", "scripts/check_public_tree.py"], cwd=ROOT, check=True)
    sdk = Path(os.environ["DEVKITPRO"])
    sdk_source = Path(os.environ["LIBNX_SOURCE_DIR"])
    if command("git", "-C", str(sdk_source), "rev-parse", "HEAD") != LIBNX_COMMIT:
        raise ValueError("Installed libnx must be built in place from the pinned source")
    if command("git", "-C", str(sdk_source), "status", "--porcelain"):
        raise ValueError("libnx source tree is dirty")
    # -B prevents a stale ignored artifact from silently entering a new release.
    subprocess.run(["make", "-B", "switch-build"], cwd=ROOT, check=True)
    output.mkdir(parents=True, exist_ok=False)
    epoch = int(command("git", "show", "-s", "--format=%ct", "HEAD"))
    source_names = command("git", "ls-files").splitlines()
    source = {name: (ROOT / name).read_bytes() for name in source_names}
    common_names = ["LICENSE", "NOTICE.md", "licenses/decoder-MIT.txt", "licenses/libnx-ISC.txt",
                    "docs/BUILD.md", "docs/COMPATIBILITY.md", "docs/MODULE.md", "docs/TEST_PLAN.md",
                    "docs/RELEASING.md", "docs/releases/v" + version + ".md"]
    common = {name: source[name] for name in common_names}
    probe = (ROOT / "tools/amazon-remote-probe/amazon-remote-probe.nro").read_bytes()
    module = (ROOT / "tools/amazon-remote-module/build/exefs.nsp").read_bytes()
    control = (ROOT / "tools/amazon-remote-module/build/amazon-remote-control.nro").read_bytes()
    for name, data in {"probe": probe, "control": control}.items():
        if data[16:20] != b"NRO0":
            raise ValueError(name + " is not an NRO")
    if module[:4] != b"PFS0":
        raise ValueError("Module is not an ExeFS PFS0 container")
    note = ("EXPERIMENTAL developer build, not verified background controller support.\n"
            "Requires the custom public MissionControl observer; stock MissionControl is insufficient.\n"
            "Read docs/TEST_PLAN.md and the release notes. No observer binary is bundled.\n"
            "Do NOT extract the module candidate directly as an active SD install.\n"
            "candidate/ is deliberately outside live SD paths. No boot2.flag or target config is provided.\n"
            "Do not replace working files or activate anything without supervised backup/testing.\n"
            "Remote wake, bonding/reconnect and microphone audio are NOT supported/verified.\n").encode()
    prefix = "switch-amazon-remote-v" + version
    archive(output / (prefix + "-source.zip"), source, epoch)
    archive(output / (prefix + "-probe.zip"), {**common, "READ-ME-FIRST.txt": note,
            "switch/amazon-remote-probe/amazon-remote-probe.nro": probe}, epoch)
    archive(output / (prefix + "-module-INACTIVE.zip"), {**common, "READ-ME-FIRST.txt": note,
            "candidate/atmosphere/contents/" + MODULE_ID + "/exefs.nsp": module,
            "candidate/switch/amazon-remote-control/amazon-remote-control.nro": control}, epoch)
    compiler = sdk / "devkitA64/bin/aarch64-none-elf-g++"
    provenance = {
        "schema": 1, "version": version, "source_commit": commit,
        "toolchain_image": image, "compiler_version": command(str(compiler), "-dumpfullversion"),
        "libnx_source_commit": libnx_commit, "libnx_archive_sha256": digest((sdk / "libnx/lib/libnx.a").read_bytes()),
        "sdk_tools_sha256": {name: digest((sdk / "tools/bin" / name).read_bytes())
                             for name in ("elf2nro", "elf2nso", "nacptool", "npdmtool", "build_pfs0")},
        "artifact_sha256": {"probe.nro": digest(probe), "exefs.nsp": digest(module), "control.nro": digest(control)},
        "archive_sha256": {p.name: digest(p.read_bytes()) for p in sorted(output.glob("*.zip"))},
        "evidence_boundary": "Public rebuilt binaries have not been device-tested; module is inactive and unverified. Host tests are not hardware proof.",
        "missing_dependency": "Custom passive MissionControl observer; source and patch in source archive, not a bundled binary.",
    }
    (output / "PROVENANCE.json").write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n")
    sums = "".join(digest(p.read_bytes()) + "  " + p.name + "\n" for p in sorted(output.iterdir()) if p.is_file())
    (output / "SHA256SUMS").write_text(sums)
    print("Packaged experimental v" + version + " from " + commit)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--toolchain-image", required=True)
    parser.add_argument("--libnx-commit", required=True)
    args = parser.parse_args()
    package(args.output.resolve(), args.toolchain_image, args.libnx_commit)
