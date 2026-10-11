#!/usr/bin/env python3
"""Verify an existing pinned checkout with both public patches applied.

Read-only source checks. No console access, download or firmware patching.
"""
import argparse
from pathlib import Path
import re
import subprocess

PIN = "71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e"
ROOT = Path(__file__).resolve().parents[1]
CORE = "mc_mitm/source/bluetooth_mitm/bluetooth/bluetooth_core.cpp"
INCLUDE = '#include "../../amazon_remote/managed_observer_runtime.hpp"\n'
TAP = """            // Observe once before existing PIN handling/name changes/forwarding.
            // Metadata only: no independent event read, response or bond claim.
            ams::mc::amazon_observer::ObserveCore(static_cast<u32>(g_current_event_type),
                &g_event_info, sizeof(g_event_info));
"""

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    assert subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip() == PIN
    original = subprocess.check_output(["git", "show", f"{PIN}:{CORE}"], cwd=source, text=True)
    candidate = (source / CORE).read_text()
    assert candidate.count(INCLUDE) == 1 and candidate.count(TAP) == 1
    assert candidate.replace(INCLUDE, "").replace(TAP, "") == original, "Existing forwarding/PIN handler changed"
    assert candidate.index(TAP) > candidate.index("R_ABORT_UNLESS(btdrvGetEventInfo(")
    assert candidate.index(TAP) < candidate.index("// Process custom event and return")
    header = (source / "mc_mitm/source/mc/mc_service.hpp").read_text()
    service = (source / "mc_mitm/source/mc/mc_service.cpp").read_text()
    assert header.count("65005,") == 1 and "GetCoreSecurityEvidence" in header
    assert service.count("Result MissionControlService::GetCoreSecurityEvidence(") == 1
    for name in ("core_security_evidence.hpp", "managed_observer_runtime.cpp", "managed_observer_runtime.hpp"):
        relative = Path("mc_mitm/source/amazon_remote") / name
        assert (source / relative).read_bytes() == (ROOT / relative).read_bytes(), "Stale staged source"
    runtime = (ROOT / "mc_mitm/source/amazon_remote/managed_observer_runtime.cpp").read_text()
    observe = runtime.split("void ObserveCore(", 1)[1].split("bool ReadCoreSecurityEvidence(", 1)[0]
    getter = runtime.split("bool ReadCoreSecurityEvidence(", 1)[1].split("bool ArmTargetCapture(", 1)[0]
    assert "g_capture.Active(now)" in observe and "hos::Version_12_0_0" in observe
    assert "size != sizeof(::amazon_remote::CoreSecurityWire)" in getter
    observe = re.sub(r"//[^\n]*", "", observe)
    getter = re.sub(r"//[^\n]*", "", getter)
    for unsafe in ("btdrv", "btm", "fs::", "Wait(", "Respond", "CreateBond", "new "):
        assert unsafe not in observe and unsafe not in getter, "Observer/getter gained side effects"
    print("PASS pinned public integration: one tap before forwarding; original core handler byte-for-byte preserved; exact staged source, fixed getter, lease/layout guards and no observer radio/storage/response calls. NOT hardware proof.")

if __name__ == "__main__":
    main()
