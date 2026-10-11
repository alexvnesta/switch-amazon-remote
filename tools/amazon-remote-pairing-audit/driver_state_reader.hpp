// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include <cstddef>
#include <cstdint>

namespace ardriver {
inline constexpr std::uint32_t NotAttempted=0xffffffff, InvalidEvidence=0xfffffffe;
inline constexpr std::uint32_t Firmware=(22u<<16)|(5u<<8), Command=100;
struct Snapshot {
    std::uint32_t magic{0x41524453}, version{1};
    std::uint64_t begin_ms{}, end_ms{};
    std::uint32_t open_result{NotAttempted}, query_result{NotAttempted};
    std::uint32_t validation_result{NotAttempted}, enabled_raw{};
    std::uint32_t firmware{Firmware}, command{Command}, reserved[2]{};
    std::uint32_t checksum{}, tail{};
};
static_assert(sizeof(Snapshot)==64 && offsetof(Snapshot,checksum)==56);
inline std::uint32_t Checksum(const Snapshot &s) {
    const auto *p=reinterpret_cast<const unsigned char *>(&s);
    std::uint32_t v=2166136261;
    for(std::size_t i=0;i<offsetof(Snapshot,checksum);++i)v=(v^p[i])*16777619;
    return v;
}
inline bool Valid(const Snapshot &s) {
    if(s.magic!=0x41524453 || s.version!=1 || s.firmware!=Firmware || s.command!=Command ||
       s.reserved[0] || s.reserved[1] || s.tail || s.checksum!=Checksum(s) ||
       !s.begin_ms || s.end_ms<s.begin_ms || s.end_ms-s.begin_ms>2000)return false;
    if(s.open_result)return s.query_result==NotAttempted &&
        s.validation_result==NotAttempted && !s.enabled_raw;
    if(s.query_result)return s.validation_result==NotAttempted && !s.enabled_raw;
    if(s.validation_result)return s.validation_result==InvalidEvidence && !s.enabled_raw;
    return s.enabled_raw<=1;
}
class Backend {
public:
    std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
    Result Open(){return smGetService(&driver_,"btdrv");}
    Result Read(std::uint8_t &out){return serviceDispatchOut(&driver_,100,out);}
    void Close(){if(serviceIsActive(&driver_))serviceClose(&driver_);}
private:
    Service driver_{};
};
// Open one owned session, issue one one-byte getter, close that session.
// No Bluetooth initialization, event consumers, radio changes or bond calls.
template<class Reader> Snapshot Collect(Reader &reader) {
    Snapshot s{};std::uint8_t raw=0xff;
    s.begin_ms=reader.Now();s.open_result=reader.Open();
    if(!s.open_result)s.query_result=reader.Read(raw);
    reader.Close();s.end_ms=reader.Now();
    if(!s.open_result && !s.query_result){
        const bool valid=raw<=1 && s.begin_ms && s.end_ms>=s.begin_ms &&
            s.end_ms-s.begin_ms<=2000;
        s.validation_result=valid?0:InvalidEvidence;
        if(valid)s.enabled_raw=raw;
    }
    s.checksum=Checksum(s);return s;
}
// An error or false response does not establish which backend is selected.
inline bool EnabledObserved(const Snapshot &s){
    return Valid(s) && !s.open_result && !s.query_result && !s.validation_result && s.enabled_raw==1;
}
}
