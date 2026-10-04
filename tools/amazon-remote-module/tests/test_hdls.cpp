// SPDX-License-Identifier: GPL-2.0-only
#include "../hdls_backend.hpp"
#include "../kernel_heap.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
#include <algorithm>
namespace {
unsigned fail_step{}, calls{};
Result memory_query_result{},heap_result{};
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
int main() {
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
    std::puts("PASS HDLS owned writable data/heap guards, neutral replies, every failure stage, cleanup/quarantine, one-shot latch and historical kernel heap setup. NOT hardware proof.");
}
