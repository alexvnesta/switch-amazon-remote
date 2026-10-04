#!/usr/bin/env python3
"""Check the publication boundary; never connects to a console or service."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
if (ROOT / ".git").exists():
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=ROOT, text=True).splitlines()
    paths = [ROOT / name for name in names]
else:
    paths = [p for p in ROOT.rglob("*") if p.is_file() and not any(part in {"build", ".git", "__pycache__"} for part in p.relative_to(ROOT).parts)]

failures = []
for path in paths:
    relative = path.relative_to(ROOT)
    # Only the reviewed HID protocol fixture is a binary source asset.
    if path.suffix.lower() in {".nsp", ".nro", ".elf", ".nso", ".npdm", ".log", ".dmp", ".heic", ".zip", ".keys"}:
        failures.append(f"Forbidden publication asset: {relative}")
    if path.suffix == ".bin":
        if path.name != "k7q3m7-descriptor.bin" or len(path.read_bytes()) != 149:
            failures.append(f"Unreviewed binary source fixture: {relative}")
        continue
    try: text = path.read_text()
    except UnicodeDecodeError:
        failures.append(f"Unreviewed binary file: {relative}"); continue
    checks = [r"/" + r"Users/[^/\s]+/", r"192\.168\.\d+\.\d+", r"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})",
              r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"]
    if any(re.search(pattern,text) for pattern in checks):
        failures.append(f"Private path/network/credential pattern: {relative}")
    for mac in re.findall(r"\b(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\b", text):
        if mac.upper() not in {"02:AB:CD:12:34:56", "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF"}:
            failures.append(f"Non-synthetic Bluetooth address: {relative}")
    # Tests only use this synthetic locally administered address prefix.
    for raw in re.findall(r"0x[0-9a-fA-F]{2}(?:\s*,\s*0x[0-9a-fA-F]{2}){5}",text):
        numbers = [int(part.strip(),16) for part in raw.split(",")]
        # This is a hygiene check, not a generic detector of all binary secrets.
        if numbers[:2] == [172,65]: failures.append(f"Unredacted device-address byte fixture: {relative}")

assert not failures, "\n".join(failures)
assert (ROOT / "LICENSE").is_file() and (ROOT / "NOTICE.md").is_file()
print(f"PASS publication hygiene: {len(paths)} reviewed source/docs files; no personal network/device dumps, paths, credentials or build outputs")
