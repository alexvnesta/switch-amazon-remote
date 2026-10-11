// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "../../mc_mitm/source/amazon_remote/core_security_evidence.hpp"
namespace arsecurity {
inline constexpr std::uint32_t NotAttempted=0xffffffff, InvalidEvidence=0xfffffffe;
struct Snapshot {
    std::uint32_t magic{0x41525341},version{1};
    std::uint64_t begin_ms{},end_ms{};
    std::uint32_t open_result{NotAttempted},query_result{NotAttempted};
    std::uint32_t validation_result{NotAttempted},reserved0{};
    amazon_remote::CoreSecurityWire evidence{};
    std::uint32_t checksum{},reserved1{};
};
static_assert(sizeof(Snapshot)==192 && offsetof(Snapshot,evidence)==40 && offsetof(Snapshot,checksum)==184);
inline std::uint32_t Checksum(const Snapshot &s) {
    const auto *p=reinterpret_cast<const unsigned char *>(&s);std::uint32_t value=2166136261;
    for(std::size_t i=0;i<offsetof(Snapshot,checksum);++i)value=(value^p[i])*16777619;
    return value;
}
inline bool Valid(const Snapshot &s) {
    if(s.magic!=0x41525341 || s.version!=1 || s.reserved0 || s.reserved1 ||
        s.checksum!=Checksum(s) || !amazon_remote::ValidCoreSecurity(s.evidence))return false;
    if(s.open_result || s.query_result || s.validation_result)
        return s.evidence.target_count==0 && s.evidence.overflow==0;
    if(!s.begin_ms || s.end_ms<s.begin_ms || s.end_ms-s.begin_ms>2000)return false;
    for(unsigned i=0;i<s.evidence.target_count;++i)if(s.evidence.events[i].boot_ms>s.end_ms)return false;
    return true;
}
class Backend {
public:
    std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
    Result Open(){return smGetService(&mc_,"mc");}
    Result Read(amazon_remote::CoreSecurityWire &out){
        return serviceDispatch(&mc_,65005,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},
            .buffers={{&out,sizeof out}});
    }
    void Close(){if(serviceIsActive(&mc_))serviceClose(&mc_);}
private:
    Service mc_{};
};
// One getter only. Does not arm/rearm capture, read shared driver events,
// initialize Bluetooth, connect, pair, respond, inspect keys or attach output.
template<class Reader> Snapshot Collect(Reader &reader){
    Snapshot out{};out.begin_ms=reader.Now();out.open_result=reader.Open();
    if(!out.open_result){out.query_result=reader.Read(out.evidence);reader.Close();}
    out.end_ms=reader.Now();
    if(!out.open_result && !out.query_result){
        bool valid=amazon_remote::ValidCoreSecurity(out.evidence) && out.begin_ms &&
            out.end_ms>=out.begin_ms && out.end_ms-out.begin_ms<=2000;
        for(unsigned i=0;valid && i<out.evidence.target_count;++i)
            valid=out.evidence.events[i].boot_ms<=out.end_ms;
        out.validation_result=valid?0:InvalidEvidence;
    }
    // Never save arbitrary/partial bytes returned by a failed or malformed IPC.
    if(out.open_result || out.query_result || out.validation_result)out.evidence={};
    out.checksum=Checksum(out);return out;
}
}
