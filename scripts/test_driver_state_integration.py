#!/usr/bin/env python3
"""Read-only call-surface and operator/storage guard checks; no device access."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
TOOL=ROOT/'tools/amazon-remote-pairing-audit'
reader=(TOOL/'driver_state_reader.hpp').read_text()
main=(TOOL/'driver_state_main.cpp').read_text()
strip=lambda text:re.sub(r'//[^\n]*','',text)
calls=strip(reader)+'\n'+strip(main)
assert re.findall(r'\bserviceDispatch\w*\s*\(',calls)==['serviceDispatchOut(']
assert 'serviceDispatchOut(&driver_,100,out)' in calls
assert 'smGetService(&driver_,"btdrv")' in calls
assert reader.count('reader.Read(raw)')==1
assert calls.index('reader.Close();')<calls.index('s.end_ms=reader.Now();')
for unsafe in ('btdrvInitialize','btmu','btmInitialize','CreateBond','RemoveBond','CancelBond','GetEventInfo','InitializeBluetooth','EnableBluetooth','DisableBluetooth','GetPairedDevice','AddPairedDevice','RespondTo'):
    assert unsafe not in calls, 'Unexpected mutating/shared-event/security API'
assert 'hosversionGet()==MAKEHOSVERSION(22,5,0)' in calls
assert '!ran && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A' in calls
assert calls.index('ran=true;')<calls.index('ardriver::Collect(reader)')
assert 'O_WRONLY|O_CREAT|O_EXCL' in calls and '::fsync(fd)' in calls
assert 'std::fgetc(f)==EOF' in calls and 'std::memcmp(&s,&back,sizeof s)' in calls
assert 'NOT VERIFIED; partial preserved' in main
assert not re.search(r'\b(?:unlink|rename|remove)\s*\(',calls)
print('PASS driver reader integration: one owned getter100, no initializer/event/radio/bond calls, exact firmware/operator one-shot, exclusive save/fsync/full readback; NOT device proof')
