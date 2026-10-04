#!/usr/bin/env python3
"""SDK-free regression tests for safe, deterministic release archive construction."""
import tempfile
import json
from pathlib import Path
import unittest
import zipfile
from package_release import archive, validate_version, digest, IMAGE, LIBNX_COMMIT, MODULE_ID
from verify_release import verify


class ReleaseTests(unittest.TestCase):
    def test_version_gate(self):
        validate_version("0.2.0-alpha.1")
        for version in ["0.2.0", "v0.2.0-alpha.1", "0.2.0-alpha.0", "0.2.0;exit", "../alpha.1"]:
            with self.assertRaises(ValueError):
                validate_version(version)

    def test_archive_reproducibility_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entries = {"LICENSE": b"license", "candidate/module.nsp": b"synthetic"}
            archive(root / "a.zip", entries, 1700000000)
            archive(root / "b.zip", dict(reversed(list(entries.items()))), 1700000000)
            self.assertEqual((root / "a.zip").read_bytes(), (root / "b.zip").read_bytes())
            with zipfile.ZipFile(root / "a.zip") as result:
                self.assertEqual(result.read("LICENSE"), b"license")
            with self.assertRaises(FileExistsError):
                archive(root / "a.zip", entries, 1700000000)

    def test_forbidden_archive_entries(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ["/abs", "a/../bad", "a\\bad", "candidate/boot2.flag", "enable.txt", "config/address.txt"]:
                with self.assertRaises(ValueError):
                    archive(Path(directory) / "bad.zip", {name: b""}, 1700000000)

    def test_full_inventory_and_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prefix = "switch-amazon-remote-v0.2.0-alpha.1"
            common = {n: b"synthetic license" for n in ["LICENSE", "NOTICE.md", "licenses/decoder-MIT.txt", "licenses/libnx-ISC.txt"]}
            archive(root / (prefix + "-source.zip"), {**common, "patches/missioncontrol-observer.patch": b"synthetic"}, 1700000000)
            archive(root / (prefix + "-probe.zip"), {**common, "READ-ME-FIRST.txt": b"inactive",
                    "switch/amazon-remote-probe/amazon-remote-probe.nro": b"probe"}, 1700000000)
            archive(root / (prefix + "-module-INACTIVE.zip"), {**common, "READ-ME-FIRST.txt": b"inactive",
                    "candidate/atmosphere/contents/" + MODULE_ID + "/exefs.nsp": b"module",
                    "candidate/switch/amazon-remote-control/amazon-remote-control.nro": b"control"}, 1700000000)
            provenance = {"version": "0.2.0-alpha.1", "source_commit": "synthetic-commit",
                          "toolchain_image": IMAGE, "libnx_source_commit": LIBNX_COMMIT,
                          "archive_sha256": {p.name: digest(p.read_bytes()) for p in root.glob("*.zip")},
                          "artifact_sha256": {"probe.nro": digest(b"probe"), "control.nro": digest(b"control"), "exefs.nsp": digest(b"module")}}
            (root / "PROVENANCE.json").write_text(json.dumps(provenance))
            (root / "SHA256SUMS").write_text("".join(digest(p.read_bytes()) + "  " + p.name + "\n" for p in sorted(root.iterdir())))
            verify(root, "synthetic-commit")
            with self.assertRaises(AssertionError):
                verify(root, "different-commit")
            extra = root / "unexpected.txt"
            extra.write_text("extra")
            with self.assertRaises(AssertionError):
                verify(root, "synthetic-commit")
            extra.unlink()
            (root / (prefix + "-probe.zip")).write_bytes(b"corrupt")
            with self.assertRaises(AssertionError):
                verify(root, "synthetic-commit")


if __name__ == "__main__":
    unittest.main()
