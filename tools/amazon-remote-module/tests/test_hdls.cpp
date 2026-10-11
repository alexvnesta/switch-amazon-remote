// SPDX-License-Identifier: GPL-2.0-only
#include "../hdls_backend.hpp"
#include "../kernel_heap.hpp"
#include "../../../mc_mitm/source/amazon_remote/k7q3m7_descriptor.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <string>
namespace {
u64 tick{};
std::vector<std::string> health_logs;
void HealthLog(const char *line) { health_logs.emplace_back(line); }
unsigned fail_step{}, calls{};
Result memory_query_result{},heap_result{};
HiddbgHdlsStateList owned_snapshot{};Result snapshot_result{};
unsigned memory_type=MemType_Heap,memory_permission=Perm_Rw,memory_attribute{},alloc_calls{},free_calls{},query_calls{};
bool allocation_failure{}; std::size_t region_size=4096;
void *heap_base=reinterpret_cast<void *>(0x100000);
std::vector<void *> live;
std::vector<unsigned> order;
std::vector<std::uint64_t> outputs;
Result Call(unsigned operation) { order.push_back(operation); return ++calls == fail_step ? 42 : 0; }
void Reset(unsigned failure) {
    // Simulates kernel reclaim at process exit, not backend reuse of a
    // quarantined transfer page. ASAN must not hide leaked host allocations.
    for (auto *p:live) std::free(p);
    live.clear(); fail_step=failure; calls=0; order.clear(); outputs.clear();
    memory_query_result=heap_result=0; memory_type=MemType_Heap;memory_permission=Perm_Rw;memory_attribute=0;
    alloc_calls=free_calls=query_calls=0;allocation_failure=false;region_size=4096;
}
void *Allocate(std::size_t alignment,std::size_t size) {
    assert(alignment==4096 && size==4096); ++alloc_calls;
    if (allocation_failure) return nullptr;
    auto *p=std::aligned_alloc(alignment,size); assert(p); live.push_back(p);
    std::memset(p,0xff,size); return p;
}
void Free(void *p) {
    const auto it=std::find(live.begin(),live.end(),p); assert(it!=live.end());
    live.erase(it); ++free_calls; std::free(p);
}
}
u64 armGetSystemTick() { return tick; }
u64 armTicksToNs(u64 value) { return value*1000000; }
Result svcSetHeapSize(void **out,u64 size) { assert(size==armodule::KernelHeapSize); *out=heap_base; return heap_result; }
Result svcQueryMemory(MemoryInfo *m,u32 *page,u64 addr) {
    ++query_calls; assert(!live.empty() && addr==reinterpret_cast<std::uintptr_t>(live.back()));
    m->addr=addr; m->size=region_size; m->type=memory_type; m->perm=memory_permission;m->attr=memory_attribute;
    *page=0;return memory_query_result;
}
Result hiddbgInitialize() { return Call(1); }
Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId *out, void *buffer, std::size_t size) {
    assert(size == 4096 && reinterpret_cast<std::uintptr_t>(buffer) % 4096 == 0);
    assert(std::find(live.begin(),live.end(),buffer)!=live.end() && query_calls==alloc_calls);
    for (unsigned i=0;i<4096;++i) assert(static_cast<unsigned char *>(buffer)[i]==0);
    const auto result=Call(2); if (!result) out->id=17; return result;
}
Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle *out, const HiddbgHdlsDeviceInfo *info) {
    assert(info->deviceType == HidDeviceType_FullKey15 && info->npadInterfaceType == HidNpadInterfaceType_USB);
    const auto result=Call(3); if (!result) out->handle=29; return result;
}
Result hiddbgSetHdlsState(HiddbgHdlsHandle handle, const HiddbgHdlsState *state) {
    assert(handle.handle == 29 && state->battery_level == 4 && state->flags == 1);
    assert(!state->analog_stick_l.x && !state->analog_stick_l.y && !state->analog_stick_r.x && !state->analog_stick_r.y);
    outputs.push_back(state->buttons); return Call(4);
}
Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle handle) { assert(handle.handle==29); return Call(5); }
Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId session) { assert(session.id==17); return Call(6); }
void hiddbgExit() { Call(7); }
Result hiddbgDumpHdlsStates(HiddbgHdlsSessionId session,HiddbgHdlsStateList *out) {
    assert(session.id==17);*out=owned_snapshot;return snapshot_result;
}
void HomeReportFlow() {
    Reset(0);tick=100;snapshot_result=0;owned_snapshot={};
    owned_snapshot.total_entries=1;owned_snapshot.entries[0].handle.handle=29;
    amazon_remote::RemoteDecoder decoder;
    assert(decoder.Configure(amazon_remote::K7q3m7Descriptor,sizeof amazon_remote::K7q3m7Descriptor));
    armodule::HdlsBackend backend(Allocate,Free);armodule::OwnedController controller(backend);
    assert(!controller.Start() && outputs.back()==0);
    const u8 home[]{0x23,2,0,0},consumer_neutral[]{0,0,0,0},keyboard_neutral[]{0,0,0},up[]{0x52,0,0},select[]{0x58,0,0};
    const auto send=[&](u8 id,const u8 *bytes,std::size_t size) {
        const auto result=decoder.Ingest(id,bytes,size);
        assert(result==amazon_remote::Update::Changed || result==amazon_remote::Update::Unchanged);
        tick+=20;assert(!controller.UpdateMaintained(decoder.Buttons(),tick));
    };
    send(2,home,sizeof home);assert(outputs.back()==HiddbgNpadButton_Home);
    const auto n=outputs.size();
    send(2,home,sizeof home);send(1,keyboard_neutral,sizeof keyboard_neutral);
    assert(outputs.size()==n && decoder.Buttons()==amazon_remote::Home);
    send(1,up,sizeof up);assert(outputs.back()==(HiddbgNpadButton_Home|HidNpadButton_Up));
    send(1,keyboard_neutral,sizeof keyboard_neutral);assert(outputs.back()==HiddbgNpadButton_Home);
    send(2,consumer_neutral,sizeof consumer_neutral);assert(outputs.back()==0);
    // Three separate physical reports generate three edges, not a macro or a
    // synthetic repeat. This checks output shape, NOT acceptance by a lock UI.
    const auto before_taps=outputs.size();
    for(unsigned i=0;i<3;++i){send(2,home,sizeof home);send(2,consumer_neutral,sizeof consumer_neutral);}
    assert(outputs.size()==before_taps+6);
    for(unsigned i=0;i<6;++i)assert(outputs[before_taps+i]==(i%2?0:HiddbgNpadButton_Home));
    for(unsigned i=0;i<3;++i){send(1,select,sizeof select);assert(outputs.back()==HidNpadButton_A);send(1,keyboard_neutral,sizeof keyboard_neutral);assert(outputs.back()==0);}
    send(2,home,sizeof home);assert(controller.LastAcknowledgedButtons()==HiddbgNpadButton_Home);
    assert(!controller.Stop() && outputs.back()==0 && controller.LastAcknowledgedButtons()==0);
    assert(!controller.Attached() && live.empty());
    decoder.Disconnect();assert(!decoder.Buttons());
    std::puts("PASS captured Home report to actual HDLS backend: press/release, duplicate, independent reports, three replayed tap sequences, Select/A taps and held-Home cleanup; synthetic SDK, no screen-unlock proof");
}
int main() {
    HomeReportFlow();
    for (unsigned failure=0; failure<=7; ++failure) {
        Reset(failure);
        armodule::HdlsBackend backend(Allocate,Free); armodule::OwnedController controller(backend);
        assert(armodule::NeutralControllerCheck(controller) == (failure ? 42u : 0u));
        assert(!controller.Attached());
        for (auto buttons : outputs) assert(!buttons);
        if (!failure) assert((order == std::vector<unsigned>{1,2,3,4,4,5,6,7}));
        const auto trace=backend.Trace(100,200);
        assert(armodule::ValidHdlsTrace(trace));
        if (!failure) assert(trace.freed==1 && !trace.quarantined && trace.set_calls==2 && trace.first_set_result==0 && trace.last_set_result==0);
        if (failure==2 || failure==7) assert(trace.quarantined==1 && !free_calls);
        if (failure) { const auto count=calls; assert(controller.Start()==1 && calls==count); }
    }
    Reset(0);
    armodule::HdlsBackend backend(Allocate,Free); armodule::OwnedController controller(backend);
    assert(controller.Start()==0);
    assert(controller.Update(amazon_remote::Confirm | amazon_remote::Home)==0);
    assert(outputs.back() == (HidNpadButton_A | HiddbgNpadButton_Home));
    const auto count=calls;
    assert(controller.Update(amazon_remote::Confirm | amazon_remote::Home)==0 && calls==count);
    assert(controller.Update(0)==0 && outputs.back()==0);
    assert(controller.Stop()==0 && !controller.Attached());
    auto trace=backend.Trace(100,200); assert(trace.freed && !trace.quarantined && free_calls==1 && live.empty());
    trace.checksum++; assert(!armodule::ValidHdlsTrace(trace));
    fail_step=calls+2; // next Open succeeds, next transfer attach fails
    assert(controller.Start()==42);
    trace=backend.Trace(100,201);
    assert(trace.quarantined && !trace.freed && free_calls==1 && live.size()==1);
    assert(backend.Open()==MAKERESULT(Module_Libnx,LibnxError_AlreadyInitialized));
    Reset(0); allocation_failure=true;
    { armodule::HdlsBackend b(Allocate,Free); armodule::OwnedController c(b);
      assert(c.Start()==MAKERESULT(Module_Libnx,LibnxError_OutOfMemory) && !query_calls && !free_calls);
      assert(c.Start()==1 && alloc_calls==1); }
    for (const unsigned type:{MemType_Heap,MemType_CodeMutable}) {
        Reset(0);memory_type=type;
        armodule::HdlsBackend b(Allocate,Free); armodule::OwnedController c(b);
        assert(armodule::NeutralControllerCheck(c)==0 && free_calls==1 && live.empty());
        const auto t=b.Trace(100,200);
        assert(armodule::ValidHdlsTrace(t) && t.memory_type==type && t.attach_session_result==0 && t.set_calls==2);
    }
    for (const unsigned type:{0u,3u,6u,9u,11u,21u}) {
        Reset(0);memory_type=type;
        armodule::HdlsBackend b(Allocate,Free); armodule::OwnedController c(b);
        assert(c.Start()==KERNELRESULT(InvalidMemoryState) && free_calls==1);
        assert((order==std::vector<unsigned>{1,7})); // never submitted ineligible memory
    }
    for (unsigned invalid=0;invalid<4;++invalid) {
        Reset(0);
        if (invalid==0) memory_query_result=39;
        if (invalid==1) memory_permission=Perm_R;
        if (invalid==2) memory_attribute=1;
        if (invalid==3) region_size=4095;
        armodule::HdlsBackend b(Allocate,Free); armodule::OwnedController c(b);
        assert(c.Start()==(invalid==0?39u:KERNELRESULT(InvalidMemoryState)) && free_calls==1);
        assert((order==std::vector<unsigned>{1,7}));
    }
    Reset(0); void *start{},*end{};
    assert(!armodule::InitializeKernelHeap(start,end) && start==heap_base && static_cast<unsigned char *>(end)-static_cast<unsigned char *>(start)==armodule::KernelHeapSize);
    heap_result=77; assert(armodule::InitializeKernelHeap(start,end)==77 && !start && !end);
    Reset(0);
    { armodule::HdlsBackend b(Allocate,Free);armodule::OwnedController c(b);assert(!c.Start());
      bool present=true;owned_snapshot={};snapshot_result=47;
      assert(b.CheckOwnedDevice(present)==47 && !present);
      snapshot_result=0;
      for (int count:{-1,17,1000000}) {
          owned_snapshot.total_entries=count;present=true;
          assert(b.CheckOwnedDevice(present)==MAKERESULT(Module_Libnx,LibnxError_BadInput) && !present);
      }
      owned_snapshot={};owned_snapshot.total_entries=16;
      for (unsigned i=0;i<16;++i) owned_snapshot.entries[i].handle.handle=100+i;
      assert(!b.CheckOwnedDevice(present) && !present); // no foreign handle adopted
      owned_snapshot.entries[15].handle.handle=29;
      assert(!b.CheckOwnedDevice(present) && present);
      owned_snapshot.entries[0].handle.handle=29;
      assert(b.CheckOwnedDevice(present)==MAKERESULT(Module_Libnx,LibnxError_BadInput) && !present);
      assert(!c.Stop()); }
    Reset(0);
    { armodule::HdlsBackend b(Allocate,Free);armodule::OwnedController c(b);b.SetLogger(HealthLog,true);
      assert(!c.Start());owned_snapshot={};owned_snapshot.total_entries=1;owned_snapshot.entries[0].handle.handle=29;snapshot_result=0;
      bool present=false;tick=0;health_logs.clear();
      for (;tick<=1800000;tick+=250) assert(!b.CheckOwnedDevice(present) && present);
      assert(health_logs.size()==61);
      snapshot_result=47;assert(b.CheckOwnedDevice(present)==47 && !present && health_logs.size()==62);
      snapshot_result=0;owned_snapshot.total_entries=0;
      assert(!b.CheckOwnedDevice(present) && !present && health_logs.size()==63);
      assert(!b.CheckOwnedDevice(present) && !present && health_logs.size()==64);
      assert(!c.Stop()); }
    Reset(0);
    std::puts("PASS HDLS memory/cleanup guards and actual backend quiet-health logging: all7201 ownership queries retained; absent/error never suppressed. NOT hardware proof.");
}
