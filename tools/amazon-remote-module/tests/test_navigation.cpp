// SPDX-License-Identifier: GPL-2.0-only
// Host SDK model: validates admission/control flow, not actual device behavior.
#include "../navigation_sdk.hpp"
#include <cassert>
#include <filesystem>
#include <string_view>
namespace {
#ifdef ARMODULE_NAV12
constexpr auto ExpectedId=0x010000000000a4d6ull;
constexpr unsigned ExpectedQueries=6,ExpectedMagic=0x41524c3c;
constexpr char ExpectedDirectory[]="sdmc:/config/amazon-remote-nav12";
constexpr char ExpectedStartup[]="/config/amazon-remote-nav12/startup.bin";
constexpr char BinaryDirectory[]="sdmc:/atmosphere/contents/010000000000a4d6";
constexpr char BinaryPath[]="sdmc:/atmosphere/contents/010000000000a4d6/exefs.nsp";
#elif defined(ARMODULE_NAV11)
constexpr auto ExpectedId=0x010000000000a4d5ull;
constexpr unsigned ExpectedQueries=5,ExpectedMagic=0x41524c3b;
constexpr char ExpectedDirectory[]="sdmc:/config/amazon-remote-nav11";
constexpr char ExpectedStartup[]="/config/amazon-remote-nav11/startup.bin";
constexpr char BinaryDirectory[]="sdmc:/atmosphere/contents/010000000000a4d5";
constexpr char BinaryPath[]="sdmc:/atmosphere/contents/010000000000a4d5/exefs.nsp";
#elif defined(ARMODULE_NAV10)
constexpr auto ExpectedId=0x010000000000a4d4ull;
constexpr unsigned ExpectedQueries=4,ExpectedMagic=0x41524c3a;
constexpr char ExpectedDirectory[]="sdmc:/config/amazon-remote-nav10";
constexpr char ExpectedStartup[]="/config/amazon-remote-nav10/startup.bin";
constexpr char BinaryDirectory[]="sdmc:/atmosphere/contents/010000000000a4d4";
constexpr char BinaryPath[]="sdmc:/atmosphere/contents/010000000000a4d4/exefs.nsp";
#else
constexpr auto ExpectedId=0x010000000000a4d3ull;
constexpr unsigned ExpectedQueries=3,ExpectedMagic=0x41524c39;
constexpr char ExpectedDirectory[]="sdmc:/config/amazon-remote-nav9";
constexpr char ExpectedStartup[]="/config/amazon-remote-nav9/startup.bin";
constexpr char BinaryDirectory[]="sdmc:/atmosphere/contents/010000000000a4d3";
constexpr char BinaryPath[]="sdmc:/atmosphere/contents/010000000000a4d3/exefs.nsp";
#endif
Result query_init{},query_rc=0x20f,shell_rc{},launch_rc{};
u64 query_pid{},tick=5000,active_id{};
unsigned query_calls{},query_exits{},shell_calls{},launch_calls{},shell_exits{};
std::size_t hashed{}; auto sha=armodule::NavigationModuleHash;
void Reset() {
    query_init=shell_rc=launch_rc=0;query_rc=0x20f;query_pid=active_id=0;tick=5000;
    query_calls=query_exits=shell_calls=launch_calls=shell_exits=0;sha=armodule::NavigationModuleHash;
    std::filesystem::remove(armodule::NavigationAttemptPath);
    std::filesystem::remove(armodule::NavigationAttemptTemp);
    std::filesystem::remove(armodule::LinkPendingPath);
}
void Write(const char *path,std::size_t bytes) {
    auto *f=std::fopen(path,"wb");assert(f);
    for (std::size_t i=0;i<bytes;++i) assert(std::fputc(0,f)==0);
    assert(!std::fclose(f));
}
armodule::NativeResult Prior() {
    using namespace armodule;
    NativeResult r{};r.stage=3;r.instance=10;r.now_ms=10000;r.permit_issued_ms=9990;r.permit_consumed=1;
    r.heap_result=r.sm_result=r.fs_result=r.mount_result=r.firmware_result=0;
    auto &t=r.trace;t.instance=10;t.now_ms=10000;t.allocation_ok=1;
    t.open_result=t.memory_query_result=t.attach_session_result=t.attach_device_result=0;
    t.first_set_result=t.last_set_result=t.detach_result=t.release_result=0;
    t.set_calls=2;t.closed=t.freed=1;t.checksum=HdlsTraceChecksum(t);r.checksum=NativeChecksum(r);
    return r;
}
}
u64 armGetSystemTick() { return tick; }
u64 armTicksToNs(u64 t) { return t*1000000; }
Result pmdmntInitialize() { return query_init; }
Result pmdmntGetProcessId(u64 *pid,u64 id) {
    assert(id==armodule::SafeModuleProgramId || id==armodule::NativeHdlsProgramId || id==armodule::ModuleProgramId || id==0x010000000000a4d3 || id==0x010000000000a4d4 || id==0x010000000000a4d5);
    ++query_calls;*pid=id==active_id?201:query_pid;return id==active_id?0:query_rc;
}
void pmdmntExit() { ++query_exits; }
Result pmshellInitialize() { ++shell_calls;return shell_rc; }
Result pmshellLaunchProgram(unsigned flags,const NcmProgramLocation *l,u64 *pid) {
    assert(!flags && l->program_id==armodule::ModuleProgramId && !l->storageID);
    for (auto b:l->reserved) assert(!b);
    ++launch_calls;*pid=launch_rc?0:200;return launch_rc;
}
void pmshellExit() { ++shell_exits; }
void sha256ContextCreate(Sha256Context *) { hashed=0; }
void sha256ContextUpdate(Sha256Context *,const void *,std::size_t n) { hashed+=n; }
void sha256ContextGetHash(Sha256Context *,void *out) { std::memcpy(out,sha.data(),32); }
int main() {
    using namespace armodule;
    static_assert(!StartupDiagnosticOnly && ModuleProgramId==ExpectedId);
    static_assert(std::string_view(Directory)==ExpectedDirectory);
    static_assert(std::string_view(StartupFsPath)==ExpectedStartup);
    auto prior=Prior();assert(PriorNativePassed(prior));
    prior.trace.detach_result=1;prior.trace.checksum=HdlsTraceChecksum(prior.trace);prior.checksum=NativeChecksum(prior);
    assert(!PriorNativePassed(prior));prior=Prior();prior.checksum^=1;assert(!PriorNativePassed(prior));prior=Prior();
    char directory[]="/tmp/amazon-navigation-test-XXXXXX";assert(mkdtemp(directory));
    const auto original=std::filesystem::current_path();std::filesystem::current_path(directory);
    for (auto p:{Directory,"sdmc:/config/amazon-remote-module","sdmc:/config/amazon-remote",
                 BinaryDirectory}) std::filesystem::create_directories(p);
    const auto path=BinaryPath;
    std::array<std::uint8_t,32> hash{};assert(!VerifyNavigationBinary(hash));
    Write(path,NavigationModuleSize-1);assert(!VerifyNavigationBinary(hash));
    Write(path,NavigationModuleSize+1);assert(!VerifyNavigationBinary(hash));
    Write(path,NavigationModuleSize);assert(VerifyNavigationBinary(hash) && hashed==NavigationModuleSize);
    sha[0]^=1;assert(!VerifyNavigationBinary(hash));Reset();
    NativeAttempt attempt{};RequestNavigationLaunch(attempt);assert(!query_calls && !launch_calls);
    assert(FixSaveExclusive(NativeResultPath,prior));
    for (auto p:{NavigationAttemptPath,NavigationAttemptTemp,LinkPendingPath}) {
        Write(p,1);RequestNavigationLaunch(attempt);assert(!query_calls && !launch_calls && std::filesystem::file_size(p)==1);Reset();
    }
    tick=10000;RequestNavigationLaunch(attempt);assert(!query_calls);Reset();
    tick=10001;RequestNavigationLaunch(attempt);assert(!query_calls);Reset();
    query_init=77;RequestNavigationLaunch(attempt);assert(!query_calls && !query_exits && !shell_calls);Reset();
    query_rc=78;RequestNavigationLaunch(attempt);assert(query_exits==1 && !shell_calls);Reset();
    for (auto id:{SafeModuleProgramId,NativeHdlsProgramId,std::uint64_t(0x010000000000a4d3),ModuleProgramId}) {
        active_id=id;RequestNavigationLaunch(attempt);assert(!shell_calls && query_exits==1);Reset();
    }
#if defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
    active_id=0x010000000000a4d4;RequestNavigationLaunch(attempt);assert(!shell_calls && query_exits==1);Reset();
#endif
#ifdef ARMODULE_NAV12
    active_id=0x010000000000a4d5;RequestNavigationLaunch(attempt);assert(!shell_calls && query_exits==1);Reset();
#endif
    sha[0]^=1;RequestNavigationLaunch(attempt);assert(!shell_calls && query_calls==ExpectedQueries);Reset();
    shell_rc=79;RequestNavigationLaunch(attempt);assert(!launch_calls && !shell_exits);
    NativeAttempt saved{};assert(ReadExact(NavigationAttemptPath,saved) && saved.magic==ExpectedMagic && saved.checksum==NativeChecksum(saved) && saved.stage==2);
    RequestNavigationLaunch(attempt);assert(shell_calls==1);Reset();
    launch_rc=80;RequestNavigationLaunch(attempt);assert(launch_calls==1 && shell_exits==1);
    assert(ReadExact(NavigationAttemptPath,saved) && saved.launch_result==80 && !saved.returned_pid);Reset();
    RequestNavigationLaunch(attempt);assert(launch_calls==1 && shell_exits==1 && query_calls==ExpectedQueries);
    assert(ReadExact(NavigationAttemptPath,saved) && saved.magic==ExpectedMagic && saved.stage==3 && !saved.launch_result &&
        saved.returned_pid==200 && saved.module_sha256==NavigationModuleHash && saved.checksum==NativeChecksum(saved));
    RequestNavigationLaunch(attempt);assert(launch_calls==1);
    std::filesystem::current_path(original);std::filesystem::remove_all(directory);
    std::puts("PASS navigation: isolated namespace, prior native proof, reboot/lock/process guards, exact binary pin, exclusive one-shot zero-flag safe-ID launch; NOT navigation hardware proof.");
}
