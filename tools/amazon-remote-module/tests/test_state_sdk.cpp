// SPDX-License-Identifier: GPL-2.0-only
// Host model tests the actual getter adapter; not service/ABI hardware proof.
#include "../state_sdk.hpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

namespace {
Result init_rc{}, query_rc{}, info_rc{}, state_rc{}, jit_rc{}, close_rc{};
u64 returned_pid=137, returned_program=armodule::ModuleProgramId;
s64 returned_state=2;
Handle returned_handle=27;
bool hint=true;
u32 jit_count=1;
std::vector<std::string> calls;
Service service;
void Reset() {
    init_rc=query_rc=info_rc=state_rc=jit_rc=close_rc=0; returned_pid=137;
    returned_program=armodule::ModuleProgramId; returned_state=2; returned_handle=27;
    hint=true; jit_count=1; calls.clear();
}
bool Called(const char *s) { return std::find(calls.begin(), calls.end(), s) != calls.end(); }
}
Result pmdmntInitialize() { calls.push_back("init"); return init_rc; }
void pmdmntExit() { calls.push_back("exit"); }
Result pmdmntGetProcessId(u64 *out, u64 program) {
    assert(program == armodule::ModuleProgramId); calls.push_back("query"); *out=returned_pid; return query_rc;
}
Service *pmdmntGetServiceSession() { return &service; }
Result serviceDispatchImpl(Service *s, u32 command, const void *in, u32 in_size, void *out, u32 out_size, SfDispatchParams params) {
    assert(s == &service && command == 65000 && in_size == 8 && out_size == 32);
    assert(*static_cast<const u64 *>(in) == returned_pid && returned_pid);
    assert(params.out_handle_attrs.attr0 == SfOutHandleAttr_HipcCopy && params.out_handles);
    calls.push_back("copy");
    if (!info_rc) { *params.out_handles=returned_handle; static_cast<armodule::ProcessInfoOutput *>(out)->location.program_id=returned_program; }
    return info_rc;
}
Result pmdmntGetJitDebugProcessIdList(u32 *count, u64 *pids, std::size_t capacity) {
    calls.push_back("jit"); assert(capacity == 32); *count=jit_count;
    if (jit_count) pids[0]=returned_pid;
    return jit_rc;
}
bool envIsSyscallHinted(unsigned svc) { assert(svc == 0x7c); calls.push_back("hint"); return hint; }
Result svcGetProcessInfo(s64 *state, Handle handle, ProcessInfoType type) {
    assert(handle == 27 && type == ProcessInfoType_ProcessState); calls.push_back("state"); *state=returned_state; return state_rc;
}
Result svcCloseHandle(Handle handle) { assert(handle == 27); calls.push_back("close"); return close_rc; }
u64 armGetSystemTick() { return 9000; }
u64 armTicksToNs(u64 value) { return value*1000000; }

int main() {
    Reset(); auto record=armodule::CaptureProcessState(1);
    assert(armodule::ValidStateRecord(record) && record.state_valid && record.state == 2 && record.jit_found);
    assert((calls == std::vector<std::string>{"init","query","jit","copy","hint","state","close","exit"}));
    for (s64 value=0; value<=7; ++value) {
        Reset(); returned_state=value; record=armodule::CaptureProcessState(2);
        assert(armodule::ValidStateRecord(record) && record.state_valid && record.state == value);
    }
    Reset(); init_rc=19; record=armodule::CaptureProcessState(1);
    assert(!record.state_valid && record.query_result == armodule::NotAttempted && calls.size() == 1);
    Reset(); query_rc=0x20f; record=armodule::CaptureProcessState(1);
    assert(!Called("copy") && !Called("jit") && Called("exit") && !record.state_valid);
    Reset(); returned_pid=0; record=armodule::CaptureProcessState(1); assert(!Called("copy"));
    Reset(); info_rc=25; record=armodule::CaptureProcessState(1); assert(!Called("state") && !Called("close"));
    Reset(); returned_program++; record=armodule::CaptureProcessState(1);
    assert(!record.identity_matches && !Called("state") && Called("close"));
    Reset(); hint=false; record=armodule::CaptureProcessState(1);
    assert(!record.syscall_hinted && !record.state_attempted && !Called("state") && Called("close"));
    Reset(); returned_handle=0; record=armodule::CaptureProcessState(1); assert(!Called("state") && !Called("close"));
    Reset(); state_rc=31; record=armodule::CaptureProcessState(1);
    assert(record.state_result == 31 && !record.state_valid && Called("close"));
    Reset(); returned_state=8; record=armodule::CaptureProcessState(1); assert(!record.state_valid);
    Reset(); jit_count=32; record=armodule::CaptureProcessState(1); assert(record.jit_at_capacity && record.jit_found);
    Reset(); jit_count=100; record=armodule::CaptureProcessState(1); assert(record.jit_at_capacity);
    Reset(); jit_count=0; record=armodule::CaptureProcessState(1); assert(!record.jit_found && !record.jit_at_capacity);
    Reset(); jit_rc=41; record=armodule::CaptureProcessState(1); assert(!record.jit_found && record.state_valid);
    Reset(); close_rc=45; record=armodule::CaptureProcessState(1); assert(record.close_result == 45 && Called("exit"));
    record.checksum++; assert(!armodule::ValidStateRecord(record));
    Reset(); record=armodule::CaptureProcessState(1);
    char directory[]="/tmp/amazon-state-test-XXXXXX"; assert(mkdtemp(directory));
    const std::string path=std::string(directory)+"/owned.bin";
    assert(armodule::SaveStateExclusive(path.c_str(),record));
    armodule::StateRecord saved{}; assert(armodule::ReadExact(path.c_str(),saved));
    assert(!armodule::SaveStateExclusive(path.c_str(),record));
    assert(std::memcmp(&record,&saved,sizeof record) == 0);
    assert(std::remove(path.c_str()) == 0 && rmdir(directory) == 0);
    std::puts("state getter adapter, identity/hint guards, bounded census, cleanup and exclusive evidence: passed");
}
