// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace arnativebond {
inline constexpr std::uint32_t Unknown=0xffffffff, Refusal=0xfffffffe;
enum class State:std::uint32_t {Refused=1,IntentSaved,AcceptedOutcomeUnknown,RequestFailed};
struct Record {
    std::uint32_t magic{0x41524e42},version{1},state{std::uint32_t(State::Refused)},open_result{Unknown};
    std::uint64_t instance{},begin_ms{},end_ms{};
    std::uint32_t handle{},readiness_result{Unknown},getter_result{Unknown},enabled{},request_result{Unknown},request_issued{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(Record)==72 && offsetof(Record,checksum)==64);
inline std::uint32_t Checksum(const Record &r){
    auto *p=reinterpret_cast<const unsigned char *>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(Record,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
inline bool Valid(const Record &r){
    if(r.magic!=0x41524e42 || r.version!=1 || r.reserved || r.checksum!=Checksum(r) ||
       r.state<1 || r.state>4 || r.enabled>1 || r.request_issued>1 ||
       !r.begin_ms || r.end_ms<r.begin_ms || r.end_ms-r.begin_ms>3000)return false;
    const bool issued=r.state==unsigned(State::AcceptedOutcomeUnknown) || r.state==unsigned(State::RequestFailed);
    if(bool(r.request_issued)!=issued)return false;
    if(issued && (!r.instance || !r.handle || r.open_result || r.readiness_result || r.getter_result || !r.enabled))return false;
    if(r.state==unsigned(State::AcceptedOutcomeUnknown))return r.request_result==0;
    if(r.state==unsigned(State::RequestFailed))return r.request_result!=0 && r.request_result!=Unknown;
    return r.request_result==Unknown;
}
struct Binding {std::uint64_t instance{};std::uint32_t handle{};};
// Only ordinary public request IPCs. No shared-event consumption, initialize,
// RemoveBond/CancelBond/response calls or key reads. Caller's guard establishes
// the exact configured physical remote and the verified live Nav17 READY state.
class Driver {
public:
    Result Open(){return smGetService(&service_,"btdrv");}
    Result Enabled(std::uint8_t &out){return serviceDispatchOut(&service_,100,out);}
    Result Request(BtdrvAddress target){
        const struct {BtdrvAddress target;std::uint8_t padding[2];std::uint32_t transport;} input{target,{0,0},2};
        static_assert(sizeof(input)==12 && offsetof(decltype(input),transport)==8);
        // ABI field2 is provided, but reviewed22.5.0 ignores this field. It
        // does NOT force LE. Success means request accepted, never bond proof.
        return serviceDispatchIn(&service_,10,input);
    }
    void Close(){if(serviceIsActive(&service_))serviceClose(&service_);}
private:Service service_{};
};
// Consumed even on refusal. Persistence must use an exclusive durable intent;
// caller revalidates after I/O. No polling-triggered request or retry exists.
class Trial {
    bool consumed_{};
public:
    template<class Backend> Record Attempt(Backend &b,BtdrvAddress target){
        Record r{};r.begin_ms=b.Now();
        const auto finish=[&]{r.end_ms=b.Now();r.checksum=Checksum(r);return r;};
        if(consumed_)return finish();
        consumed_=true;
        unsigned any=0,all=255;for(auto byte:target.address){any|=byte;all&=byte;}
        if(!any || all==255 || !b.Supported() || !b.OperatorConfirmed())return finish();
        Binding bound{};r.readiness_result=b.Guard(target,bound);
        r.instance=bound.instance;r.handle=bound.handle;
        if(r.readiness_result || !r.instance || !r.handle || r.handle==0xffffffff)return finish();
        r.open_result=b.Open();
        if(r.open_result){b.Close();return finish();}
        std::uint8_t enabled=0xff;r.getter_result=b.Enabled(enabled);
        if(r.getter_result || enabled!=1){b.Close();return finish();}
        r.enabled=1;r.state=unsigned(State::IntentSaved);r.end_ms=b.Now();r.checksum=Checksum(r);
        if(!Valid(r) || !b.PersistIntent(r)){b.Close();r.state=unsigned(State::Refused);return finish();}
        Binding after{};r.readiness_result=b.Guard(target,after);
        const auto now=b.Now();
        if(r.readiness_result || after.instance!=bound.instance || after.handle!=bound.handle ||
           now<r.begin_ms || now-r.begin_ms>2000){
            b.Close();r.state=unsigned(State::Refused);if(!r.readiness_result)r.readiness_result=Refusal;return finish();
        }
        r.request_issued=1;r.request_result=b.Request(target);b.Close();
        r.state=unsigned(r.request_result?State::RequestFailed:State::AcceptedOutcomeUnknown);
        return finish();
    }
};
}
