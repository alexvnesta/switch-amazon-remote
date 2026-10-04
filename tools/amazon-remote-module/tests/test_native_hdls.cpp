// SPDX-License-Identifier: GPL-2.0-only
// Native-launch wire/adapter tests under a host SDK model, not device execution.
#include "../native_hdls_sdk.hpp"
#include <cassert>
#include <filesystem>
namespace {
Result query_init{},query_rc=0x20f,shell_rc{},launch_rc{};
u64 query_pid{},tick=5000;
unsigned query_calls{},query_exits{},shell_calls{},launch_calls{},shell_exits{};
std::size_t hashed{}; auto sha=armodule::NativeModuleHash;
void Reset() {
    query_init=shell_rc=launch_rc=0;query_rc=0x20f;query_pid=0;tick=5000;
    query_calls=query_exits=shell_calls=launch_calls=shell_exits=0;sha=armodule::NativeModuleHash;
}
void ClearEvidence() {
    for (auto p:{armodule::NativePermitPath,armodule::NativeUsedPath,armodule::NativeAttemptPath,armodule::NativeAttemptTemp,armodule::NativeResultPath,armodule::NativeResultTemp})
        std::filesystem::remove(p);
}
void Write(const char *path,std::size_t bytes) {
    auto *f=std::fopen(path,"wb");assert(f);
    for (std::size_t i=0;i<bytes;++i) assert(std::fputc(0,f)==0);
    assert(!std::fclose(f));
}
}
u64 armGetSystemTick() { return tick; }
u64 armTicksToNs(u64 t) { return t*1000000; }
Result pmdmntInitialize() { return query_init; }
Result pmdmntGetProcessId(u64 *pid,u64 id) { assert(id==armodule::NativeHdlsProgramId);++query_calls;*pid=query_pid;return query_rc; }
void pmdmntExit() { ++query_exits; }
Result pmshellInitialize() { ++shell_calls;return shell_rc; }
Result pmshellLaunchProgram(unsigned flags,const NcmProgramLocation *l,u64 *pid) {
    assert(!flags && l->program_id==armodule::NativeHdlsProgramId && !l->storageID);
    for (auto b:l->reserved) assert(!b);
    ++launch_calls;*pid=launch_rc?0:200;return launch_rc;
}
void pmshellExit() { ++shell_exits; }
void sha256ContextCreate(Sha256Context *) { hashed=0; }
void sha256ContextUpdate(Sha256Context *,const void *,std::size_t n) { hashed+=n; }
void sha256ContextGetHash(Sha256Context *,void *out) { std::memcpy(out,sha.data(),32); }
int main() {
    using namespace armodule;
    static_assert(NativeHdlsProgramId<0x0100000000010000);
    NativePermit permit{};permit.issued_ms=5000;permit.checksum=NativeChecksum(permit);
    assert(ValidNativePermit(permit,5000) && ValidNativePermit(permit,7000));
    assert(!ValidNativePermit(permit,4999) && !ValidNativePermit(permit,7001));
    permit.magic=0x41525037;permit.checksum=NativeChecksum(permit);assert(!ValidNativePermit(permit,5000));
    permit={};permit.issued_ms=5000;
    permit.neutral_only=0;permit.checksum=NativeChecksum(permit);assert(!ValidNativePermit(permit,5000));
    permit={};permit.issued_ms=5000;permit.program_id++;permit.checksum=NativeChecksum(permit);assert(!ValidNativePermit(permit,5000));
    permit={};permit.issued_ms=5000;permit.buttons_released=0;permit.checksum=NativeChecksum(permit);assert(!ValidNativePermit(permit,5000));
    permit={};permit.issued_ms=5000;permit.checksum=NativeChecksum(permit);
    NativeResult result{};result.stage=1;result.instance=10;result.now_ms=5001;result.permit_issued_ms=5000;result.permit_consumed=1;result.checksum=NativeChecksum(result);
    assert(ValidNativeResult(result));result.stage=3;result.checksum=NativeChecksum(result);assert(!ValidNativeResult(result));
    result.trace.instance=10;result.trace.now_ms=5001;result.trace.checksum=HdlsTraceChecksum(result.trace);result.checksum=NativeChecksum(result);
    assert(ValidNativeResult(result));result.trace.instance++;result.trace.checksum=HdlsTraceChecksum(result.trace);result.checksum=NativeChecksum(result);assert(!ValidNativeResult(result));
    char directory[]="/tmp/amazon-native-test-XXXXXX";assert(mkdtemp(directory));
    const auto original=std::filesystem::current_path();std::filesystem::current_path(directory);
    std::filesystem::create_directories("sdmc:/config/amazon-remote-module");
    std::filesystem::create_directories("sdmc:/atmosphere/contents/010000000000a4d2");
    constexpr char path[]="sdmc:/atmosphere/contents/010000000000a4d2/exefs.nsp";
    std::array<std::uint8_t,32> hash{};assert(!VerifyNativeBinary(hash));
    Write(path,NativeModuleSize-1);assert(!VerifyNativeBinary(hash));
    Write(path,NativeModuleSize+1);assert(!VerifyNativeBinary(hash));
    Write(path,NativeModuleSize);assert(VerifyNativeBinary(hash) && hashed==NativeModuleSize);
    sha[0]^=1;assert(!VerifyNativeBinary(hash));Reset();
    NativeAttempt attempt{};
    for (auto p:{NativePermitPath,NativeUsedPath,NativeAttemptPath,NativeAttemptTemp,NativeResultPath,NativeResultTemp}) {
        Write(p,1);RequestNativeTest(attempt);assert(!query_calls && !launch_calls && std::filesystem::file_size(p)==1);ClearEvidence();
    }
    query_init=77;RequestNativeTest(attempt);assert(!query_calls && !query_exits && !shell_calls);
    Reset();query_rc=78;RequestNativeTest(attempt);assert(query_exits==1 && !shell_calls);
    Reset();query_rc=0;query_pid=201;RequestNativeTest(attempt);assert(!shell_calls);
    Reset();sha[0]^=1;RequestNativeTest(attempt);assert(!shell_calls);
    Reset();shell_rc=79;RequestNativeTest(attempt);assert(!launch_calls && !shell_exits);
    NativeAttempt saved{};assert(ReadExact(NativeAttemptPath,saved) && ValidNativeAttempt(saved) && saved.stage==2 && saved.shell_result==79);
    NativePermit saved_permit{};assert(ReadExact(NativePermitPath,saved_permit) && ValidNativePermit(saved_permit,tick));
    RequestNativeTest(attempt);assert(shell_calls==1);ClearEvidence();
    Reset();launch_rc=80;RequestNativeTest(attempt);assert(launch_calls==1 && shell_exits==1);
    assert(ReadExact(NativeAttemptPath,saved) && ValidNativeAttempt(saved) && saved.launch_result==80 && !saved.returned_pid);ClearEvidence();
    Reset();RequestNativeTest(attempt);assert(launch_calls==1 && shell_exits==1);
    assert(ReadExact(NativeAttemptPath,saved) && ValidNativeAttempt(saved) && !saved.launch_result && saved.returned_pid==200 && saved.module_sha256==NativeModuleHash);
    assert(ReadExact(NativePermitPath,saved_permit) && ValidNativePermit(saved_permit,tick));
    assert(FixSaveExclusive(NativeUsedPath,saved_permit) && !FixSaveExclusive(NativeUsedPath,saved_permit));
    RequestNativeTest(attempt);assert(launch_calls==1); // reopening never creates second permission
    std::filesystem::current_path(original);std::filesystem::remove_all(directory);
    std::puts("PASS native diagnostic: strict neutral/fresh permit, wire/instance checks, fixed zero-flag safe-ID launch, exact binary admission, exclusive intent/used marker, service failures and no second attempt. NOT hardware proof.");
}
