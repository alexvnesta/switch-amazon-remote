// SPDX-License-Identifier: GPL-2.0-only
#include "../control/driver_identity_sdk.hpp"
#include <cassert>
#include <cstdio>
namespace {
struct State {
    unsigned pm_open{}, pm_close{}, pid_calls{}, loader_open{}, loader_close{}, calls{};
    Result pm_error{}, loader_error{}, modules_error{};
    s32 count=1;u64 now=1000;
} state;
}
std::uint32_t hosversionGet(){return MAKEHOSVERSION(22,5,0);}
u64 armGetSystemTick(){return state.now++;}
u64 armTicksToNs(u64 t){return t*1000000;}
Result pmdmntInitialize(){++state.pm_open;return state.pm_error;}
void pmdmntExit(){++state.pm_close;}
Result pmdmntGetProcessId(u64 *out,u64 program){
    assert(program==ardriverid::ProgramId);++state.pid_calls;*out=55;return 0;
}
Result ldrDmntInitialize(){++state.loader_open;return state.loader_error;}
void ldrDmntExit(){++state.loader_close;}
Result ldrDmntGetProcessModuleInfo(u64 pid,LoaderModuleInfo *out,std::size_t cap,s32 *n){
    assert(pid==55 && cap==ardriverid::Capacity && out && n);++state.calls;
    for(unsigned i=0;i<cap;++i){
        // Ensure raw load addresses/sizes cannot end up in the saved report.
        out[i].base_address=0xdeadbeef;out[i].size=0x12345678;
        std::memcpy(out[i].build_id,ardriverid::Expected.data(),32);
    }
    *n=state.count;return state.modules_error;
}
int main(){
    using namespace ardriverid;Backend backend;
    auto r=Collect(backend);
    assert(Valid(r) && r.verdict==unsigned(Verdict::KnownBuildMetadata));
    assert(r.build_ids[0]==Expected);
    for(unsigned i=1;i<Capacity;++i)assert(r.build_ids[i]==BuildId{});
    assert(state.pm_open==1 && state.pm_close==1 && state.pid_calls==2 && state.loader_open==1 && state.loader_close==1 && state.calls==1);
    for(s32 n:{-1,0,16,17,2147483647}){
        state={};state.count=n;r=Collect(backend);
        assert(Valid(r) && r.verdict!=unsigned(Verdict::KnownBuildMetadata));
        for(const auto &id:r.build_ids)assert(id==BuildId{});
    }
    state={};state.modules_error=9;r=Collect(backend);
    assert(Valid(r) && r.verdict==unsigned(Verdict::QueryFailed));
    for(const auto &id:r.build_ids)assert(id==BuildId{});
    state={};state.loader_error=9;r=Collect(backend);
    assert(Valid(r) && !state.loader_close && state.pm_close==1 && !state.calls);
    state={};state.pm_error=9;r=Collect(backend);
    assert(Valid(r) && !state.pm_close && !state.loader_open && !state.calls);
    std::puts("PASS I1 actual SDK adapter: exact program/PID/capacity, bounded ID-only projection, corrupt counts and service failures; no addresses/keys, live process memory or Bluetooth IPC.");
}
