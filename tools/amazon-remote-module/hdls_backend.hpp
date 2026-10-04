// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "controller.hpp"
#include "hdls_trace.hpp"
#include <cstdlib>
#include <cstring>

namespace armodule {
static_assert(HidNpadButton_A == (1ull<<0) && HidNpadButton_B == (1ull<<1));
static_assert(HidNpadButton_Plus == (1ull<<10) && HidNpadButton_Left == (1ull<<12));
static_assert(HidNpadButton_Up == (1ull<<13) && HidNpadButton_Right == (1ull<<14));
static_assert(HidNpadButton_Down == (1ull<<15) && HiddbgNpadButton_Home == (1ull<<18));
class HdlsBackend {
public:
    using Allocate=void *(*)(std::size_t,std::size_t);
    using Free=void (*)(void *);
    explicit HdlsBackend(Allocate allocate=std::aligned_alloc,Free release=std::free)
        : allocate_(allocate),free_(release) {}
    HdlsBackend(const HdlsBackend &)=delete;
    HdlsBackend &operator=(const HdlsBackend &)=delete;
    Result Open() {
        if (buffer_ || (trace_.open_result==0 && !trace_.closed)) return MAKERESULT(Module_Libnx,LibnxError_AlreadyInitialized);
        trace_={}; transfer_attempted_=release_confirmed_=false; session_={};handle_={};
        return trace_.open_result=hiddbgInitialize();
    }
    Result AttachSession() {
        buffer_=allocate_(0x1000,0x1000);
        trace_.buffer_address=reinterpret_cast<std::uintptr_t>(buffer_);
        trace_.allocation_ok=buffer_!=nullptr;
        if (!buffer_) return MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);
        MemoryInfo memory{}; u32 page{};
        trace_.memory_query_result=svcQueryMemory(&memory,&page,trace_.buffer_address);
        if (trace_.memory_query_result) return trace_.memory_query_result;
        trace_.region_address=memory.addr; trace_.region_size=memory.size;
        trace_.memory_type=memory.type; trace_.permission=memory.perm; trace_.attributes=memory.attr;
        // Require an unshared writable kernel-heap page covering the whole
        // allocation. Alignment alone never establishes transfer eligibility.
        if (trace_.buffer_address%0x1000 || (memory.type&0xff)!=MemType_Heap || memory.perm!=Perm_Rw || memory.attr ||
            trace_.buffer_address<memory.addr || trace_.buffer_address-memory.addr>memory.size ||
            memory.size-(trace_.buffer_address-memory.addr)<0x1000) return KERNELRESULT(InvalidMemoryState);
        std::memset(buffer_,0,0x1000); transfer_attempted_=true;
        return trace_.attach_session_result=hiddbgAttachHdlsWorkBuffer(&session_,buffer_,0x1000);
    }
    Result AttachDevice() {
        HiddbgHdlsDeviceInfo info{};
        // Matches the public sys-con HDLS Pro-controller convention. This is a
        // virtual pad, not a claim that the remote is a native Switch device.
        info.deviceType = HidDeviceType_FullKey15;
        info.npadInterfaceType = HidNpadInterfaceType_USB;
        info.singleColorBody = 0x222222ff; info.singleColorButtons = 0xaaaaaaff;
        return trace_.attach_device_result=hiddbgAttachHdlsVirtualDevice(&handle_, &info);
    }
    Result Set(std::uint64_t buttons) {
        HiddbgHdlsState state{};
        state.buttons = buttons; state.battery_level = 4; state.flags = 1;
        const auto rc=hiddbgSetHdlsState(handle_, &state);
        if (!trace_.set_calls) trace_.first_set_result=rc;
        ++trace_.set_calls; trace_.last_set_result=rc; return rc;
    }
    Result DetachDevice() { return trace_.detach_result=hiddbgDetachHdlsVirtualDevice(handle_); }
    Result ReleaseSession() {
        const auto rc=trace_.release_result=hiddbgReleaseHdlsWorkBuffer(session_);
        release_confirmed_=rc==0; return rc;
    }
    void Close() {
        hiddbgExit(); trace_.closed=1;
        if (buffer_) {
            if (!transfer_attempted_ || release_confirmed_) { free_(buffer_); buffer_=nullptr; trace_.freed=1; }
            else trace_.quarantined=1; // bounded 4KiB kept until process exit; never reused after ambiguous cleanup
        }
    }
    HdlsTrace Trace(std::uint64_t instance,std::uint64_t now) const {
        auto r=trace_; r.instance=instance; r.now_ms=now; r.checksum=HdlsTraceChecksum(r); return r;
    }
private:
    Allocate allocate_; Free free_; void *buffer_{};
    bool transfer_attempted_{},release_confirmed_{};
    HdlsTrace trace_{};
    HiddbgHdlsSessionId session_{};
    HiddbgHdlsHandle handle_{};
};
}
