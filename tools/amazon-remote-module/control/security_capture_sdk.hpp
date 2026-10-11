// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "security_capture.hpp"
#include "driver_identity_sdk.hpp"
#include "../io.hpp"
#include <cerrno>
#include <sys/stat.h>
namespace arsecuritycapture {
#ifdef ARMODULE_BOND_PROPERTIES
inline constexpr char Path[]="sdmc:/switch/amazon-remote-navigation17/bond-properties-m3.bin";
inline constexpr unsigned ReadCommand=65009;
#else
inline constexpr char Path[]="sdmc:/switch/amazon-remote-navigation17/security-events-m2.bin";
inline constexpr unsigned ReadCommand=65008;
#endif
inline bool Absent() {struct stat s{};return ::stat(Path,&s)!=0 && errno==ENOENT;}
class Backend {
public:
    std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
    std::uint32_t Nonce(){const auto n=std::uint32_t(armGetSystemTick())^0x6d32724a;return n?n:1;}
    void Wait(){svcSleepThread(10000000);}
    ardriverid::Record Identity(){ardriverid::Backend b;return ardriverid::Collect(b);}
    Result Read(Wire&w) {
        Service mc{};auto rc=smGetService(&mc,"mc");if(rc)return rc;
        Wire candidate{};
        rc=serviceDispatch(&mc,ReadCommand,.buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},
            .buffers={{&candidate,sizeof candidate}});
        serviceClose(&mc);
        if(!rc && !metadata::Valid(candidate))return Invalid;
        if(!rc)w=candidate;
        return rc;
    }
    Result Probe(std::uint32_t nonce) {
        Service mc{};auto rc=smGetService(&mc,"mc");if(rc)return rc;
        rc=serviceDispatchIn(&mc,65007,nonce);serviceClose(&mc);return rc;
    }
    bool Save(const Report&r) {
        if(!Valid(r))return false;
        FILE*f=std::fopen(Path,"wbx");if(!f)return false;
        const bool wrote=std::fwrite(&r,1,sizeof r,f)==sizeof r;
        const bool flushed=std::fflush(f)==0, synced=::fsync(::fileno(f))==0;
        const bool closed=std::fclose(f)==0;Report back{};
        return wrote&&flushed&&synced&&closed&&armodule::ReadExact(Path,back)&&
            !std::memcmp(&back,&r,sizeof r)&&Valid(back);
    }
};
}
