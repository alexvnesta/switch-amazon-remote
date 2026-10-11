#!/usr/bin/env python3
"""Check the actual owner build, not only the retained standalone source."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
strip=lambda s:re.sub(r'//[^\n]*','',s)
read=lambda p:strip((root/p).read_text())
gui=read('tools/amazon-remote-pairing-audit/native_bond_owner_main.cpp')
session=read('mc_mitm/source/amazon_remote/libnx_session.cpp').split('LibnxSession::NativeBondOnce',1)[1].split('LibnxSession::EnrollOnce',1)[0]
driver=read('mc_mitm/source/amazon_remote/native_gatt_requests.hpp').split('#ifdef ARMODULE_NATIVE_OWNER',1)[1].split('#endif',1)[0]
main=read('tools/amazon-remote-module/main.cpp')
branch=main.split('else if(command.kind==unsigned(armodule::CommandKind::NativeBond))',1)[1].split('else if(command.kind==unsigned(armodule::CommandKind::Enroll))',1)[0]
assert 'VerifyNavigationBinary(hash)' in gui and 'pmdmntGetProcessId(&pid,armodule::ModuleProgramId)' in gui
assert 'down==HidNpadButton_A && buttons==HidNpadButton_A' in gui
assert gui.count('armodule::Publish(')==1 and 'MatchesOwnerCommand(result,instance,sequence)' in gui
assert 'A command is NOT proof' in gui and 'Whether a request was sent is UNKNOWN' in gui
for forbidden in ('btmInitialize(', 'btmuInitialize(', 'smGetService(', 'serviceDispatch', 'pmshellLaunchProgram(', 'trial.Attempt('):
    assert forbidden not in gui,forbidden
assert session.count('native_bond_trial_.Attempt(')==1 and session.count('btmBleGetConnectionState(')==1
assert session.count('CheckCurrentTarget()')==2 and 's.gatt_.SystemOpen()' in session and 's.native_.Active()' in session
assert 'SystemReadyBinding(env.status,env.startup,env.context,Now(),s.connection_)' in session
assert 'void Close(){}' in session and 'owner.native_.BondRequest(target)' in session
for forbidden in ('smGetService(', 'btmInitialize(', 'btmuInitialize(', 'btmExit(', 'serviceClose(', 'PairDevice(', 'RemoveBond(', 'CancelBond('):
    assert forbidden not in session,forbidden
assert re.findall(r'\bserviceDispatch\w*\s*\(',driver)==['serviceDispatchOut(','serviceDispatchIn(']
assert 'serviceDispatchOut(&service_,100,out)' in driver and 'serviceDispatchIn(&service_,10,in)' in driver
assert 'sizeof(in)==12 && offsetof(decltype(in),transport)==8' in driver
assert 'remote.NativeBondOnce(command.sequence,env,NativeIntent)' in branch and 'SaveNativeExclusive(arnativebond::OwnerResultPath,outcome)' in branch
assert main.count('remote.NativeBondOnce(')==1
assert (root/'tools/amazon-remote-pairing-audit/native_bond_owner_main.cpp').is_file()
post=read('tools/amazon-remote-pairing-audit/native_postcheck_main.cpp')
assert 'native_postcheck_main.cpp' in (root/'tools/amazon-remote-pairing-audit/Makefile').read_text()
assert 'collector.Once(' in post and 'SaveNativeExclusive(arpostcheck::Path,record)' in post
assert 'down==HidNpadButton_A && buttons==HidNpadButton_A' in post
for forbidden in ('armodule::Publish(', 'NativeBond)', 'trial.Attempt(', 'serviceDispatch', 'smGetService(', 'pmshellLaunchProgram(', 'std::remove(', 'std::rename('):
    assert forbidden not in post,forbidden
print('PASS owner request policy retained unchanged; current Post1 frontend uses one getter-only capture, exclusive evidence, no command publication or request path.')
