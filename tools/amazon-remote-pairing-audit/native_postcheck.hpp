// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "native_bond_owner.hpp"
#include "context.hpp"
namespace arpostcheck {
enum class Guard:std::uint32_t {Passed,Preflight,Owner,Intent,Status,Changed};
// This is a correlation guard for getters, never permission to pair/connect.
inline Guard Check(const arnativebond::OwnerResult &owner,const arnativebond::Record &intent,
                   const armodule::Status &status,std::uint64_t now){
    using namespace arnativebond;
    const auto &t=owner.trial;
    if(!ValidOwnerResult(owner) || t.state!=unsigned(State::AcceptedOutcomeUnknown) ||
       owner.count!=2 || !t.request_issued || t.request_result)return Guard::Owner;
    for(const auto &c:owner.checks)if(c.instance!=t.instance || c.handle!=t.handle ||
        c.user_result || c.system_result || c.matches!=1 || !c.count || c.count>=16 || c.invalid ||
        c.begin_ms<t.begin_ms || c.end_ms>t.end_ms)return Guard::Owner;
    if(!Valid(intent) || intent.state!=unsigned(State::IntentSaved) || intent.instance!=t.instance ||
       intent.handle!=t.handle || intent.begin_ms!=t.begin_ms || intent.end_ms>t.end_ms ||
       intent.open_result || intent.readiness_result || intent.getter_result || !intent.enabled)return Guard::Intent;
    if(!armodule::FreshStatus(status,now) || status.instance!=t.instance ||
       status.state!=unsigned(armodule::State::Stopped) || status.result || status.output_attached ||
       status.pad_cleanup_result || status.buttons || status.link_ready || status.capture_deadline ||
       status.last_sequence<=owner.sequence || status.now_ms<t.end_ms)return Guard::Status;
    return Guard::Passed;
}
struct Record {
    std::uint32_t magic{0x41525034},version{1};
    std::uint64_t instance{},sequence{},request_end_ms{};
    std::uint32_t owner_checksum{},intent_checksum{},guard{unsigned(Guard::Preflight)},reserved0{};
    araudit::ContextSnapshot snapshot{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(Record)==184 && offsetof(Record,checksum)==176);
inline std::uint32_t Checksum(const Record &r){
    const auto *p=reinterpret_cast<const unsigned char*>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(Record,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
inline bool Valid(const Record &r){
    const auto &s=r.snapshot;
    return r.magic==0x41525034 && r.version==1 && !r.reserved0 && !r.reserved &&
        r.guard<=unsigned(Guard::Changed) && r.checksum==Checksum(r) &&
        s.magic==0x41525033 && s.version==3 && !s.reserved0 && !s.reserved &&
        s.checksum==araudit::Checksum(s) && s.pairing_started<=1;
}
inline bool TimedAfterRequest(const Record &r){
    const auto &s=r.snapshot;
    return Valid(r) && r.guard==unsigned(Guard::Passed) && r.instance && r.sequence &&
        r.request_end_ms && s.begin_ms>=r.request_end_ms && s.end_ms>=s.begin_ms &&
        s.end_ms-s.begin_ms<=2000;
}
class Collector {
    bool consumed_{};
public:
    template<class Backend> Record Once(Backend &b,const araudit::Address &target,
        const arnativebond::OwnerResult &owner,const arnativebond::Record &intent,
        const armodule::Status &status,std::uint64_t now,bool preflight){
        Record r{};r.instance=owner.trial.instance;r.sequence=owner.sequence;r.request_end_ms=owner.trial.end_ms;
        r.owner_checksum=owner.checksum;r.intent_checksum=intent.checksum;
        r.snapshot.begin_ms=r.snapshot.end_ms=now;
        r.guard=unsigned(consumed_ || !preflight?Guard::Preflight:Check(owner,intent,status,now));
        consumed_=true;
        if(!r.guard)r.snapshot=araudit::CollectContext(b,target);
        else r.snapshot.checksum=araudit::Checksum(r.snapshot);
        r.checksum=Checksum(r);return r;
    }
};
inline constexpr char Path[]="sdmc:/switch/amazon-remote-native-bond-trial/postcheck.bin";
}
