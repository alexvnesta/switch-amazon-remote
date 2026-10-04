// SPDX-License-Identifier: GPL-2.0-only
// Tests the actual adapter's guarded requests with a host SDK model only.
#include "../memory_sdk.hpp"
#include <cassert>
#include <cstdlib>
#include <vector>
#include <string>
namespace {
constexpr u64 base=0x100000;
std::array<unsigned char,4096> alias{},code{},note{},data{};
Service service; VirtmemReservation reservation;
unsigned hints=15, maps{},unmaps{},locks{},unlocks{},reservations{},queries{};
unsigned map_fail{},unmap_fail{};
unsigned code_type=MemType_CodeStatic,code_attr{},code_permissions=Perm_R|Perm_X;
u64 code_size=0x2b000;
Result init_rc{},query_rc{},info_rc{},state_rc{};
bool wrong_identity{},bad_attribute{},no_space{},never_code{},query_failure{};
bool wide_unmapped_tail{},wide_mapped_tail{};
u64 pid=137;
void Reset() {
    hints=15; maps=unmaps=locks=unlocks=reservations=queries=map_fail=unmap_fail=0;
    init_rc=query_rc=info_rc=state_rc=0; pid=137;
    code_type=MemType_CodeStatic;code_size=0x2b000;code_attr=0;code_permissions=Perm_R|Perm_X;
    wrong_identity=bad_attribute=no_space=never_code=query_failure=false;
    wide_unmapped_tail=wide_mapped_tail=false;
    alias.fill(0);code.fill(0);note.fill(0);data.fill(0);
    std::memcpy(code.data()+0xaf0,armodule::CodeFingerprint.data(),64);
    std::memcpy(note.data()+0x590,armodule::BuildNote.data(),36);
    armodule::StartupRecord r{};r.stage=unsigned(armodule::StartupStage::Sm);r.instance=11;r.now_ms=12;
    r.checksum=armodule::StartupChecksum(r);std::memcpy(data.data(),&r,sizeof r);
}
}
u64 armGetSystemTick() { return 9000; }
u64 armTicksToNs(u64 n) { return n*1000000; }
bool envIsSyscallHinted(unsigned svc) {
    if (svc==0x74) return hints&1;
    if (svc==0x75) return hints&2;
    if (svc==0x76) return hints&4;
    assert(svc==0x7c);return hints&8;
}
Result pmdmntInitialize() {return init_rc;}
void pmdmntExit() {}
Result pmdmntGetProcessId(u64 *out,u64 id) {assert(id==armodule::ModuleProgramId);*out=pid;return query_rc;}
Service *pmdmntGetServiceSession() {return &service;}
Result serviceDispatchImpl(Service *s,u32 cmd,const void *in,u32 insize,void *out,u32 outsize,SfDispatchParams params) {
    assert(s==&service && cmd==65000 && insize==8 && outsize==32 && *static_cast<const u64 *>(in)==pid);
    assert(params.out_handle_attrs.attr0==SfOutHandleAttr_HipcCopy);
    if (!info_rc) {*params.out_handles=27;static_cast<armodule::ProcessInfoOutput *>(out)->location.program_id=armodule::ModuleProgramId+wrong_identity;}
    return info_rc;
}
Result svcCloseHandle(Handle h) {assert(h==27 && locks==unlocks);return 0;}
Result svcGetProcessInfo(s64 *out,Handle h,ProcessInfoType type) {
    assert(h==27 && type==ProcessInfoType_ProcessState);u32 value=2;std::memcpy(out,&value,4);return state_rc;
}
Result svcQueryProcessMemory(MemoryInfo *out,u32 *page,Handle h,u64 address) {
    assert(h==27);++queries;*page=0;
    if (query_failure) return 55;
    if (never_code) {*out={address,4096,0,0,0};return 0;}
    if (address<base) *out={0,base,0,0,0};
    else if (address<base+code_size) *out={base,code_size,code_type,code_attr,code_permissions};
    else if (address<base+0x34000) *out={base+code_size,0x34000-code_size,MemType_CodeStatic,0,Perm_R};
    else if (address<base+0x36000) *out={base+0x34000,0x2000,MemType_CodeMutable,0,Perm_R|2};
    else *out={base+0x36000,wide_unmapped_tail||wide_mapped_tail?u64(0)-(base+0x36000):(1ull<<39)-(base+0x36000),wide_mapped_tail?MemType_CodeStatic:0,0,0};
    if (bad_attribute && address==base+0x35000) out->attr=1;
    return 0;
}
void virtmemLock() {++locks;assert(locks==unlocks+1);}
void virtmemUnlock() {++unlocks;assert(locks==unlocks);}
void *virtmemFindCodeMemory(std::size_t size,std::size_t guard) {assert(size==4096 && guard==4096);return no_space?nullptr:alias.data();}
VirtmemReservation *virtmemAddReservation(void *p,std::size_t size) {assert(p==alias.data() && size==4096);++reservations;return &reservation;}
Result svcMapProcessMemory(void *dst,Handle h,u64 source,u64 size) {
    assert(dst==alias.data() && h==27 && size==4096);++maps;if (maps==map_fail) return 61;
    assert(source==base+0x1000 || source==base+0x33000 || source==base+0x35000);
    const auto &image=source==base+0x1000?code:source==base+0x33000?note:data;
    std::memcpy(dst,image.data(),4096);return 0;
}
Result svcUnmapProcessMemory(void *dst,Handle h,u64 source,u64 size) {
    assert(dst==alias.data() && h==27 && size==4096);
    assert(source==base+0x1000 || source==base+0x33000 || source==base+0x35000);
    ++unmaps;return unmaps==unmap_fail?71:0;
}
int main() {
    Reset();const auto original=data;auto r=armodule::CaptureMemoryCheckpoint(1,true);
    assert(armodule::ValidMemoryRecord(r) && r.startup_valid && r.startup.stage==1 && r.state==2 && r.state_valid);
    assert(r.base==base && r.fingerprint_valid && r.build_note_valid && r.record_coherent);
    assert(maps==3 && unmaps==3 && locks==unlocks && data==original);
    Reset();wide_unmapped_tail=true;armodule::LayoutRecord tail{};
    r=armodule::CaptureMemoryCheckpoint(1,true,&tail);assert(tail.complete && r.startup_valid && tail.entries[4].size>(1ull<<39));
    Reset();wide_mapped_tail=true;r=armodule::CaptureMemoryCheckpoint(1,true,&tail);assert(!tail.complete && !maps);
    for (unsigned type : {MemType_CodeStatic,MemType_CodeMutable}) {
        for (u64 extent : {u64(0x2000),u64(0x2b000),u64(0x2c000)}) {
            Reset();code_type=type;code_size=extent;armodule::LayoutRecord layout{};
            r=armodule::CaptureMemoryCheckpoint(1,true,&layout);
            assert(r.startup_valid && layout.complete && layout.count==5 && armodule::ValidLayoutRecord(layout));
            assert(layout.entries[1].type==type && layout.entries[1].size==extent && layout.entries[1].guards==31);
        }
    }
    for (unsigned type : {0u,5u,8u,9u}) {
        Reset();code_type=type;armodule::LayoutRecord layout{};
        r=armodule::CaptureMemoryCheckpoint(1,true,&layout);
        assert(!maps && layout.complete && layout.count==5 && !(layout.entries[1].guards&armodule::GuardCodeType));
    }
    Reset();code_size=0x1000;armodule::LayoutRecord rejected{};
    r=armodule::CaptureMemoryCheckpoint(1,true,&rejected);assert(!maps && !(rejected.entries[1].guards&armodule::GuardExtent));
    Reset();code_attr=16;r=armodule::CaptureMemoryCheckpoint(1,true,&rejected);
    assert(!maps && !(rejected.entries[1].guards&armodule::GuardAttributes));
    Reset();code_permissions=Perm_R;r=armodule::CaptureMemoryCheckpoint(1,true,&rejected);
    assert(!maps && !(rejected.entries[1].guards&armodule::GuardRx));
    Reset();r=armodule::CaptureMemoryCheckpoint(1,false);assert(!maps && !queries && r.init_result==armodule::NotAttempted);
    for (unsigned mask=0;mask<15;++mask) {Reset();hints=mask;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!maps && !queries);}
    Reset();init_rc=9;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!queries && !r.record_copied);
    Reset();query_rc=0x20f;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!queries && !maps);
    Reset();pid=0;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!queries && !maps);
    Reset();info_rc=7;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!queries && !maps);
    Reset();wrong_identity=true;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!queries && !maps);
    Reset();code[0xaf0]^=1;r=armodule::CaptureMemoryCheckpoint(1,true);assert(maps==1 && !r.record_copied);
    Reset();note[0x590]^=1;r=armodule::CaptureMemoryCheckpoint(1,true);assert(maps==2 && !r.record_copied);
    Reset();bad_attribute=true;r=armodule::CaptureMemoryCheckpoint(1,true);assert(maps==2 && !r.record_copied);
    Reset();no_space=true;r=armodule::CaptureMemoryCheckpoint(1,true);assert(!maps && locks==unlocks);
    for (unsigned n=1;n<=3;++n) {
        Reset();map_fail=n;r=armodule::CaptureMemoryCheckpoint(1,true);assert(maps==n && unmaps==n-1 && !r.record_copied && locks==unlocks);
        Reset();unmap_fail=n;r=armodule::CaptureMemoryCheckpoint(1,true);assert(maps==n && unmaps==n && reservations==1 && !r.record_copied && locks==unlocks);
    }
    Reset();data[36]^=1;r=armodule::CaptureMemoryCheckpoint(1,true);assert(r.record_copied && !r.startup_valid);
    Reset();data.fill(0);r=armodule::CaptureMemoryCheckpoint(1,true);assert(r.record_copied && !r.startup_valid);
    Reset();query_failure=true;r=armodule::CaptureMemoryCheckpoint(1,true);assert(r.memory_query_result==55 && !maps);
    Reset();never_code=true;r=armodule::CaptureMemoryCheckpoint(1,true);assert(r.regions==128 && queries==128 && !maps);
    Reset();r=armodule::CaptureMemoryCheckpoint(2,true);r.checksum++;assert(!armodule::ValidMemoryRecord(r));
    Reset();r=armodule::CaptureMemoryCheckpoint(2,true);
    char directory[]="/tmp/amazon-ram-test-XXXXXX";assert(mkdtemp(directory));const std::string path=std::string(directory)+"/owned.bin";
    assert(armodule::SaveMemoryExclusive(path.c_str(),r));assert(!armodule::SaveMemoryExclusive(path.c_str(),r));
    armodule::MemoryRecord copy{};assert(armodule::ReadExact(path.c_str(),copy) && !std::memcmp(&r,&copy,sizeof r));
    assert(std::remove(path.c_str())==0 && rmdir(directory)==0);
    Reset();armodule::LayoutRecord layout{};(void)armodule::CaptureMemoryCheckpoint(2,true,&layout);
    assert(armodule::ValidLayoutRecord(layout));layout.checksum++;assert(!armodule::ValidLayoutRecord(layout));
    layout.checksum=armodule::LayoutChecksum(layout);
    char layout_directory[]="/tmp/amazon-layout-test-XXXXXX";assert(mkdtemp(layout_directory));
    const std::string layout_path=std::string(layout_directory)+"/owned.bin";
    assert(armodule::SaveLayoutExclusive(layout_path.c_str(),layout));assert(!armodule::SaveLayoutExclusive(layout_path.c_str(),layout));
    armodule::LayoutRecord layout_copy{};assert(armodule::ReadExact(layout_path.c_str(),layout_copy) && !std::memcmp(&layout,&layout_copy,sizeof layout));
    assert(std::remove(layout_path.c_str())==0 && rmdir(layout_directory)==0);
    std::puts("PASS RAM adapter: identity/hints, exact code+build ID, page bounds, source unchanged, mapping cleanup/failures,128-region bound, checksum and exclusive evidence; not hardware proof");
}
