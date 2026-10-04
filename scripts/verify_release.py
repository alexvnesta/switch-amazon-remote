#!/usr/bin/env python3
"""Verify downloaded assets, their exact inventory and inactive package layout."""
import argparse
import json
from pathlib import Path
import zipfile
from package_release import digest, validate_version, IMAGE, LIBNX_COMMIT, MODULE_ID


def verify(directory, expected_commit):
    provenance = json.loads((directory / "PROVENANCE.json").read_text())
    version = provenance["version"]
    validate_version(version)
    assert provenance["source_commit"] == expected_commit, "Release source revision mismatch"
    assert provenance["toolchain_image"] == IMAGE and provenance["libnx_source_commit"] == LIBNX_COMMIT
    prefix = "switch-amazon-remote-v" + version
    archives = {prefix + suffix for suffix in ["-source.zip", "-probe.zip", "-module-INACTIVE.zip"]}
    inventory = archives | {"PROVENANCE.json", "SHA256SUMS"}
    assert {p.name for p in directory.iterdir()} == inventory, "Unexpected or missing release asset"
    checksums = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        checksum, name = line.split("  ", 1)
        assert name in inventory - {"SHA256SUMS"} and name not in checksums, "Invalid checksum inventory"
        checksums[name] = checksum
    assert set(checksums) == inventory - {"SHA256SUMS"}
    for name, checksum in checksums.items():
        assert digest((directory / name).read_bytes()) == checksum, "Checksum mismatch: " + name
    assert set(provenance["archive_sha256"]) == archives
    for name in archives:
        assert provenance["archive_sha256"][name] == checksums[name]
        with zipfile.ZipFile(directory / name) as package:
            assert package.testzip() is None
            assert len(package.namelist()) == len(set(package.namelist()))
            assert {"LICENSE", "NOTICE.md", "licenses/decoder-MIT.txt", "licenses/libnx-ISC.txt"} <= set(package.namelist())
            for member in package.infolist():
                assert not member.filename.startswith("/") and ".." not in Path(member.filename).parts and "\\" not in member.filename
                assert Path(member.filename).name not in {"boot2.flag", "enable.txt", "address.txt"}
                assert member.external_attr >> 16 == 0o100644, "Unexpected executable/symlink archive entry"
            if name.endswith("-source.zip"):
                assert "patches/missioncontrol-observer.patch" in package.namelist()
                assert not any(Path(n).suffix in {".nro", ".nsp", ".elf", ".log", ".keys"} for n in package.namelist())
            else:
                expected = ({"switch/amazon-remote-probe/amazon-remote-probe.nro": "probe.nro"}
                            if name.endswith("-probe.zip") else {
                                "candidate/atmosphere/contents/" + MODULE_ID + "/exefs.nsp": "exefs.nsp",
                                "candidate/switch/amazon-remote-control/amazon-remote-control.nro": "control.nro"})
                binaries = {n for n in package.namelist() if Path(n).suffix in {".nro", ".nsp", ".elf", ".npdm"}}
                assert binaries == set(expected), "Unexpected binary or active module path"
                for binary, key in expected.items():
                    assert digest(package.read(binary)) == provenance["artifact_sha256"][key]
                assert "READ-ME-FIRST.txt" in package.namelist()
    print("PASS complete release inventory, hashes, licenses and inactive layout: v" + version)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    verify(args.directory, args.commit)
