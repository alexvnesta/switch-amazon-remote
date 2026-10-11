// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "driver_identity.hpp"
#include <cstring>
namespace ardriverid {
class Backend {
public:
    static_assert(Firmware==MAKEHOSVERSION(22,5,0));
    static_assert(sizeof(LoaderModuleInfo)==48 && sizeof(LoaderModuleInfo::build_id)==32);
    std::uint32_t FirmwareVersion() { return hosversionGet(); }
    std::uint64_t Now() { return armTicksToNs(armGetSystemTick())/1000000; }
    Result PmOpen() { return pmdmntInitialize(); }
    void PmClose() { pmdmntExit(); }
    Result ProcessId(std::uint64_t program, std::uint64_t &pid) {
        u64 out{}; const auto result=pmdmntGetProcessId(&out,program);
        pid=out; return result;
    }
    Result LoaderOpen() { return ldrDmntInitialize(); }
    void LoaderClose() { ldrDmntExit(); }
    Result Modules(std::uint64_t pid, Inventory &ids, std::int32_t &count) {
        ids={};
        std::array<LoaderModuleInfo,Capacity> raw{}; s32 n=-1;
        const auto result=ldrDmntGetProcessModuleInfo(pid,raw.data(),raw.size(),&n);
        count=n;
        if (!result && n>0 && std::size_t(n)<raw.size())
            for (unsigned i=0; i<unsigned(n); ++i)
                std::memcpy(ids[i].data(),raw[i].build_id,ids[i].size());
        return result;
    }
};
}
