// SPDX-License-Identifier: GPL-2.0-only
// Tests actual adapters with host SDK stubs; not hardware permission/ABI proof.
#include "../launch_fix_sdk.hpp"
#include <cassert>
#include <filesystem>
#include <vector>
#include <string>

namespace {
Result init_rc{},old_query_rc{},safe_query_rc=armodule::FixProcessNotFound,info_rc{},close_rc{},shell_rc{},launch_rc{};
u64 old_pid=137,safe_pid{},flags=1,keys=128,info_id{};
bool mismatch{},missing_handle{};
unsigned launch_calls{},shell_calls{},shell_exits{},copy_calls{},closes{},exits{},query_calls{};
std::size_t hashed{}; auto hash=armodule::SafeModuleHash;
Service service; u64 tick=2000,last_target{};
void Reset() {
    init_rc=old_query_rc=info_rc=close_rc=shell_rc=launch_rc=0;
    safe_query_rc=armodule::FixProcessNotFound; old_pid=137;safe_pid=0;flags=1;keys=128;
    mismatch=missing_handle=false; launch_calls=shell_calls=shell_exits=copy_calls=closes=exits=query_calls=0;
    hash=armodule::SafeModuleHash; tick=2000;
}
void Write(const char *path,std::size_t size) {
    auto *f=std::fopen(path,"wb"); assert(f);
    for (std::size_t i=0;i<size;++i) assert(std::fputc(0,f)==0);
    assert(!std::fclose(f));
}
void ClearAttempt() {
    std::filesystem::remove(armodule::FixPath); std::filesystem::remove(armodule::FixTempPath);
}
}
u64 armGetSystemTick() { return tick; }
u64 armTicksToNs(u64 t) { return t*1000000; }
Result pmdmntInitialize() { return init_rc; }
Result pmdmntGetProcessId(u64 *pid,u64 target) {
    assert(target==armodule::ModuleProgramId || target==armodule::SafeModuleProgramId);
    ++query_calls; last_target=target; const bool safe=target==armodule::SafeModuleProgramId;
    *pid=safe?safe_pid:old_pid; return safe?safe_query_rc:old_query_rc;
}
void pmdmntExit() { ++exits; }
Service *pmdmntGetServiceSession() { return &service; }
Result serviceDispatchImpl(Service *s,u32 cmd,const void *in,u32 in_size,void *out,u32 out_size,SfDispatchParams p) {
    assert(s==&service && cmd==65000 && in_size==8 && out_size==32 && *static_cast<const u64 *>(in));
    assert(p.out_handle_attrs.attr0==SfOutHandleAttr_HipcCopy && p.out_handles);
    ++copy_calls;
    if (!info_rc) {
        *p.out_handles=missing_handle?0:37;
        auto &i=*static_cast<armodule::ProcessInfoOutput *>(out);
        i.location.program_id=mismatch?last_target+1:last_target; info_id=i.location.program_id;
        i.override_status.keys_held=keys; i.override_status.flags=flags;
    }
    return info_rc;
}
Result svcCloseHandle(Handle h) { assert(h==37); ++closes; return close_rc; }
void sha256ContextCreate(Sha256Context *) { hashed=0; }
void sha256ContextUpdate(Sha256Context *,const void *,std::size_t n) { hashed+=n; }
void sha256ContextGetHash(Sha256Context *,void *out) { std::memcpy(out,hash.data(),32); }
Result pmshellInitialize() { ++shell_calls; return shell_rc; }
Result pmshellLaunchProgram(unsigned f,const NcmProgramLocation *loc,u64 *pid) {
    assert(f==PmLaunchFlag_None && loc->program_id==armodule::SafeModuleProgramId && loc->storageID==NcmStorageId_None);
    for (auto byte:loc->reserved) assert(!byte);
    ++launch_calls; *pid=launch_rc?0:138; return launch_rc;
}
void pmshellExit() { ++shell_exits; }
int main() {
    using namespace armodule;
    static_assert(SafeModuleProgramId<0x0100000000010000);
    // No launch on open/held A; a new deliberate edge after neutral is required.
    ReleasedLaunchGate gate;
    assert(!gate.Poll(100,true,false)); assert(!gate.Poll(101,false,true));
    assert(!gate.Poll(2000,false,true)); assert(!gate.Consumed());
    assert(!gate.Poll(2001,true,false)); assert(!gate.Poll(2002,false,true));
    assert(!gate.Poll(3001,false,true)); assert(gate.Poll(3002,false,true));
    assert(!gate.Poll(5000,true,true) && gate.Consumed());
    ReleasedLaunchGate interrupted;
    assert(!interrupted.Poll(10,false,true)); assert(!interrupted.Poll(11,true,false));
    assert(!interrupted.Poll(12,false,true)); assert(!interrupted.Poll(1000,false,false));
    assert(!interrupted.Poll(1100,false,true)); assert(!interrupted.Poll(2099,false,true));
    assert(interrupted.Poll(2100,false,true));
    ReleasedLaunchGate backwards;
    assert(!backwards.Poll(100,false,true)); assert(!backwards.Poll(101,true,false));
    assert(!backwards.Poll(102,false,true)); assert(!backwards.Poll(10,false,true));
    assert(!backwards.Poll(5000,false,true)); // regression disarms, new A required
    Reset(); auto old=CaptureOverride(false);
    assert(ValidOverride(old) && ConfirmedLegacyHbl(old) && old.keys_held==keys && old.flags==1);
    assert(old.location_program_id==ModuleProgramId && copy_calls==1 && closes==1 && exits==1);
    auto before=CaptureOverride(true);
    assert(SafeAbsent(before) && copy_calls==1 && exits==2);
    Reset(); init_rc=19; old=CaptureOverride(false);
    assert(old.init_result==19 && old.query_result==FixNotAttempted && !ConfirmedLegacyHbl(old) && !exits);
    Reset(); old_query_rc=20; old=CaptureOverride(false); assert(!copy_calls && exits==1 && !ConfirmedLegacyHbl(old));
    Reset(); old_pid=0; old=CaptureOverride(false); assert(!copy_calls && !ConfirmedLegacyHbl(old));
    Reset(); info_rc=21; old=CaptureOverride(false); assert(!closes && !ConfirmedLegacyHbl(old));
    Reset(); mismatch=true; old=CaptureOverride(false);
    assert(!old.identity_matches && !old.hbl && old.flags==1 && closes==1 && !ConfirmedLegacyHbl(old));
    Reset(); close_rc=22; old=CaptureOverride(false); assert(!ConfirmedLegacyHbl(old) && exits==1);
    Reset(); missing_handle=true; old=CaptureOverride(false); assert(!ConfirmedLegacyHbl(old) && !closes);
    Reset(); flags=2; old=CaptureOverride(false); assert(!ConfirmedLegacyHbl(old));
    Reset(); safe_query_rc=0; before=CaptureOverride(true); assert(!SafeAbsent(before));
    Reset(); safe_pid=138; before=CaptureOverride(true); assert(!SafeAbsent(before));
    old.checksum++; assert(!ValidOverride(old));
    char directory[]="/tmp/amazon-fix-sdk-XXXXXX"; assert(mkdtemp(directory));
    const auto cwd=std::filesystem::current_path(); std::filesystem::current_path(directory);
    std::filesystem::create_directories("sdmc:/atmosphere/contents/010000000000a4d0");
    std::filesystem::create_directories("sdmc:/config/amazon-remote-module");
    FixSdk sdk; std::array<std::uint8_t,32> digest{}; Reset();
    assert(!sdk.Verify(digest)); Write(SafeModulePath,SafeModuleSize-1); assert(!sdk.Verify(digest));
    Write(SafeModulePath,SafeModuleSize+1); assert(!sdk.Verify(digest));
    Write(SafeModulePath,SafeModuleSize); assert(sdk.Verify(digest) && hashed==SafeModuleSize);
    hash[0]^=1; assert(!sdk.Verify(digest));
    FixRecord attempt{};
    Reset(); flags=0; assert(RequestFix(sdk,attempt)==FixOutcome::LegacyNotHbl && !shell_calls && !launch_calls);
    Reset(); mismatch=true; assert(RequestFix(sdk,attempt)==FixOutcome::LegacyNotHbl && !launch_calls);
    Reset(); safe_query_rc=19; assert(RequestFix(sdk,attempt)==FixOutcome::SafeNotAbsent && !shell_calls);
    Reset(); safe_query_rc=0;safe_pid=138; assert(RequestFix(sdk,attempt)==FixOutcome::SafeNotAbsent && !shell_calls);
    Reset(); hash[0]^=1; assert(RequestFix(sdk,attempt)==FixOutcome::HashMismatch && !shell_calls);
    Reset(); Write(FixPath,1); assert(RequestFix(sdk,attempt)==FixOutcome::IntentFailed && !shell_calls);
    assert(std::filesystem::file_size(FixPath)==1); ClearAttempt();
    Reset(); Write(FixTempPath,1); assert(RequestFix(sdk,attempt)==FixOutcome::IntentFailed && !shell_calls);
    assert(std::filesystem::file_size(FixTempPath)==1); ClearAttempt();
    Reset(); shell_rc=24; assert(RequestFix(sdk,attempt)==FixOutcome::ShellFailed && !launch_calls && !shell_exits);
    FixRecord saved{}; assert(ReadExact(FixPath,saved) && ValidFix(saved) && saved.stage==2 && saved.shell_result==24);
    ClearAttempt(); Reset(); launch_rc=25;
    assert(RequestFix(sdk,attempt)==FixOutcome::Returned && launch_calls==1 && shell_exits==1);
    assert(ReadExact(FixPath,saved) && ValidFix(saved) && saved.launch_result==25 && !saved.returned_pid);
    ClearAttempt(); Reset(); assert(RequestFix(sdk,attempt)==FixOutcome::Returned && launch_calls==1 && shell_exits==1);
    assert(ReadExact(FixPath,saved) && ValidFix(saved) && saved.returned_pid==138 && saved.stage==3);
    assert(RequestFix(sdk,attempt)==FixOutcome::IntentFailed && launch_calls==1); // no second launch across reopening
    safe_query_rc=0; safe_pid=138; flags=0; tick=7000; saved.after=CaptureOverride(true); saved.stage=4;
    saved.checksum=FixChecksum(saved); assert(ValidFix(saved) && sdk.Update(saved));
    Write(FixTempPath,1); assert(!sdk.Update(saved)); // preserve prior complete receipt
    FixRecord unchanged{}; assert(ReadExact(FixPath,unchanged) && !std::memcmp(&saved,&unchanged,sizeof saved));
    saved.checksum++; assert(!ValidFix(saved) && !sdk.Update(saved));
    Reset(); old=CaptureOverride(false); assert(FixSaveExclusive(OverridePath,old));
    assert(!FixSaveExclusive(OverridePath,old));
    std::filesystem::current_path(cwd); std::filesystem::remove_all(directory);
    std::puts("PASS diag6: release gate, copied-handle override/identity/cleanup, fail-closed legacy/absence/hash checks, fixed safe-ID zero-flag launch, exclusive intent and preserved partial evidence. NOT hardware proof.");
}
