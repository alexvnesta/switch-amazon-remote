// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "native_bond_trial.hpp"
#include "native_bond_guard.hpp"
#include <array>
namespace arnativebond {
// Owner evidence is distinct from the standalone client's v1/v2 guard logs.
// No extra Bluetooth clients, identity borrowing or implicit service opens.
enum class OwnerStage:std::uint32_t { LocalState=1, Metadata, UserBinding, SystemGetter, ListBounds, ListInvalid, TargetBinding, Passed };
struct OwnerCheck {
    std::uint32_t magic{0x41524f43},version{1},call{},stage{},result{Unknown},reserved{};
    std::uint64_t instance{},begin_ms{},end_ms{};
    std::uint32_t user_result{Unknown},system_result{Unknown},count{Unknown},matches{Unknown},invalid{Unknown},handle{},checksum{},padding{};
};
static_assert(sizeof(OwnerCheck)==80 && offsetof(OwnerCheck,checksum)==72);
inline std::uint32_t OwnerChecksum(const OwnerCheck &r){
    auto *p=reinterpret_cast<const unsigned char*>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(OwnerCheck,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
inline bool ValidOwnerCheck(const OwnerCheck &r){
    return r.magic==0x41524f43 && r.version==1 && !r.reserved && !r.padding && r.call>=1 && r.call<=2 &&
        r.stage>=1 && r.stage<=unsigned(OwnerStage::Passed) && r.begin_ms && r.end_ms>=r.begin_ms &&
        r.end_ms-r.begin_ms<=3000 && r.checksum==OwnerChecksum(r) &&
        (r.stage==unsigned(OwnerStage::Passed)?r.result==0:r.result!=0);
}
struct OwnerResult {
    std::uint32_t magic{0x41524f52},version{1};std::uint64_t sequence{};
    Record trial{};std::array<OwnerCheck,2> checks{};std::uint32_t count{},checksum{};
};
static_assert(sizeof(OwnerResult)==256 && offsetof(OwnerResult,checksum)==252);
inline std::uint32_t OwnerResultChecksum(const OwnerResult &r){
    auto *p=reinterpret_cast<const unsigned char*>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(OwnerResult,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
inline bool ValidOwnerResult(const OwnerResult &r){
    if(r.magic!=0x41524f52 || r.version!=1 || !r.sequence || !Valid(r.trial) || r.count>2 ||
       r.checksum!=OwnerResultChecksum(r))return false;
    for(unsigned i=0;i<r.count;++i)if(!ValidOwnerCheck(r.checks[i]) || r.checks[i].call!=i+1)return false;
    if(r.trial.request_issued && (r.count!=2 || r.checks[0].stage!=unsigned(OwnerStage::Passed) ||
       r.checks[1].stage!=unsigned(OwnerStage::Passed)))return false;
    return true;
}
struct OwnerEnvironment {
    armodule::Status status{};armodule::StartupRecord startup{};
    amazon_remote::EnrollmentContextRecord context{};
    bool pad_attached{},log_healthy{};
};
using NativeIntent=bool (*)(const Record &);
inline constexpr char IntentPath[]="sdmc:/switch/amazon-remote-native-bond-trial/intent.bin";
inline constexpr char OwnerResultPath[]=ARMODULE_NAV15_CONFIG "/owner-bond-result.bin";
// Only the physical A path publishes NativeBond; old module builds reject6.
inline bool MatchesOwnerCommand(const OwnerResult &r,std::uint64_t instance,std::uint64_t sequence){
    return ValidOwnerResult(r) && r.sequence==sequence && r.trial.instance==instance;
}
}
