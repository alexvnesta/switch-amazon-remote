#!/usr/bin/env python3
"""Check the candidate's explicit, targeted normal-API request surface."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
TOOL=ROOT/'tools/amazon-remote-pairing-audit'
main=(TOOL/'native_bond_main.cpp').read_text()
policy=(TOOL/'native_bond_trial.hpp').read_text()
guard=(TOOL/'native_bond_guard.hpp').read_text()
backend=(TOOL/'native_bond_backend.hpp').read_text()
strip=lambda s:re.sub(r'//[^\n]*','',s)
assert re.findall(r'\bserviceDispatch\w*\s*\(',strip(policy))==['serviceDispatchOut(','serviceDispatchIn(']
assert 'serviceDispatchOut(&service_,100,out)' in policy and 'serviceDispatchIn(&service_,10,input)' in policy
assert 'sizeof(input)==12 && offsetof(decltype(input),transport)==8' in policy
assert policy.count('b.Request(target)')==1
assert policy.index('consumed_=true;')<policy.index('b.Guard(target,bound)')
assert policy.index('b.PersistIntent(r)')<policy.index('b.Guard(target,after)')<policy.index('b.Request(target)')
assert 'after.instance!=bound.instance || after.handle!=bound.handle' in policy
assert 'now-r.begin_ms>2000' in policy and 'AcceptedOutcomeUnknown' in policy
assert 'VerifyNavigationBinary(hash)' in main and 'pmdmntGetProcessId(&pid,armodule::ModuleProgramId)' in main
assert 'armodule::EnrollmentContextPath' in main and 'araudit::CountConnections(query,configured,normalized,count)' in backend
assert 'SystemReadyBinding(d.status,d.startup,d.context,d.check_ms,d.live_handle)' in backend
assert 'ConnectedReady(d.status,d.startup,d.check_ms)' in backend
assert backend.count('btmBleGetConnectionState(')==1 and 'btmuBleGetConnectionState(' not in backend
assert 'btmInitialize()' in backend and 'btmExit()' in backend and 'btmuInitialize(' not in backend
assert 'ReadyBinding(s,startup,c,now,current_handle)' in guard
assert '!v.btm_open' in guard and 'q.target_handle==current_handle' in guard
assert 'q.list.count<16 && q.list.matches==1' in guard
assert 'd.aruid_zero=platform_.AruidZero()' in backend and 'appletGetAppletResourceUserId()==0' in main
assert main.index('trial.Attempt(backend,target)')<main.index('SaveNamed("guard"')
assert 'SaveNamed(' not in backend and 'PersistIntent(r)' in backend
assert 'GuardStage::TargetMissing' in backend and 'GuardStage::ContextBinding' in backend
# Producer/consumer contract regression: a fresh sighting is scan-only, not a
# connected status predicate. Keep the physical Link command's scan gate intact.
producer=(ROOT/'tools/amazon-remote-module/main.cpp').read_text()
control=(ROOT/'tools/amazon-remote-module/control/main.cpp').read_text()
view=(ROOT/'tools/amazon-remote-module/control/setup_view.hpp').read_text()
sighting=strip(producer).split('const auto sighting =',1)[1].split('while (!shutdown)',1)[0]
assert 'policy.Get() != armodule::State::Scanning' in sighting
assert 'status.link_ready = sighting(wire);' in producer
assert 'link_ready' not in strip(guard)
assert 'state.link_ready' not in strip(main)+strip(backend)
assert 'if (i.state != 3 || !i.pad_attached)' in view
assert 'status.state == unsigned(armodule::State::Scanning) && status.link_ready' in control
assert 'c.expected_handle!=current_handle' in guard and 'now-v.end_ms>60000' in guard
assert '!ran && released && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A' in main
assert 'hosversionGet()==MAKEHOSVERSION(22,5,0)' in main
assert 'O_WRONLY|O_CREAT|O_EXCL' in main and '::fsync(fd)' in main and 'std::memcmp(&record,&back,sizeof record)' in main
assert 'No retry/reset' not in main or 'No retry' in main
for forbidden in ('CreateBond(', 'btdrvInitialize(', 'btdrvGetEventInfo(', 'btmuBlePairDevice(', 'btmBlePairDevice(', 'RemoveBond(', 'CancelBond(', 'RespondTo', 'GetPairedDeviceInfo(', 'AddPairedDeviceInfo(', 'btmGetDeviceInfo(', 'btmGetShortenedDeviceInfo(', 'StartBleScan(', 'EnableBluetooth(', 'pmshellLaunchProgram(', 'btmSetAppletResourceUserId(', 'btmRegisterAppletResourceUserId(', 'btmBleOverrideConnection('):
    assert forbidden not in strip(main)+strip(policy)+strip(backend), 'Unreviewed API: '+forbidden
assert not re.search(r'\b(?:unlink|remove|rename)\s*\(',strip(main))
print('PASS native trial integration: one normal command10, command100 getter, exact12-byte ABI; explicit A, pinned Nav17/PM/READY/same-session handle, durable intent + revalidation, no resets/retries/keys/shared-event reads. NOT SMP/persistence proof.')
