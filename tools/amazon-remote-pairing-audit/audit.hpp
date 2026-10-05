// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace araudit {
using Address=std::array<std::uint8_t,6>;
inline constexpr std::uint32_t NotAttempted=0xffffffff,InvalidSnapshot=0xfffffffe;
// Copied from the first eight manufacturer AD bytes captured for the configured
// K7Q3M7. This is a query-profile hypothesis, NOT a validated enrollment policy.
struct Parameter {std::uint16_t company{0x0171};std::array<std::uint8_t,6> pattern{0x04,0x27,0x02,0x7d,0,0};};
struct Query {std::uint32_t result{NotAttempted},count{},matches{},invalid{};};
struct Snapshot {
    std::uint32_t magic{0x41525032},version{2};std::uint64_t boot_ms{};
    std::uint32_t ble_open{NotAttempted};Parameter parameter{};Query ble{};
    std::uint32_t btm_open{NotAttempted};Query hid{};
    std::uint32_t system_open{NotAttempted},pairing_query{NotAttempted},pairing_started{};
    std::uint32_t reserved0{},checksum{},reserved{};
};
static_assert(sizeof(Parameter)==8 && sizeof(Query)==16 && sizeof(Snapshot)==88 && offsetof(Snapshot,checksum)==80);
inline std::uint32_t Checksum(const Snapshot &s) {
    const auto *p=reinterpret_cast<const unsigned char *>(&s);std::uint32_t hash=2166136261;
    for(std::size_t i=0;i<offsetof(Snapshot,checksum);++i)hash=(hash^p[i])*16777619;
    return hash;
}
inline void Count(Query &q,const Address &target,const std::array<Address,10> &addresses,std::uint32_t count) {
    if(q.result==InvalidSnapshot){q.invalid=1;q.count=count;return;}
    if(q.result)return;
    q.count=count;
    if(count>addresses.size()){q.invalid=1;q.result=InvalidSnapshot;return;}
    for(unsigned i=0;i<count;++i)q.matches+=addresses[i]==target;
    if(q.matches>1){q.invalid=1;q.result=InvalidSnapshot;}
}
// Each client is independent and getter-only. No events, radio/scan setters,
// pairing request, key access, connection operation or virtual-pad operation.
template<class Backend> Snapshot Collect(Backend &b,const Address &target,std::uint64_t now) {
    Snapshot out{};out.boot_ms=now;
    out.ble_open=b.OpenBle();
    if(!out.ble_open) {
        std::array<Address,10> addresses{};std::uint32_t count{};
        out.ble.result=b.Paired(out.parameter,addresses,count);Count(out.ble,target,addresses,count);b.CloseBle();
    }
    out.btm_open=b.OpenHid();
    if(!out.btm_open) {
        std::array<Address,10> addresses{};std::uint32_t count{};
        out.hid.result=b.Hid(addresses,count);Count(out.hid,target,addresses,count);b.CloseHid();
    }
    out.system_open=b.OpenSystem();
    if(!out.system_open) {
        bool started=false;out.pairing_query=b.PairingStarted(started);
        if(!out.pairing_query)out.pairing_started=started;
        b.CloseSystem();
    }
    out.checksum=Checksum(out);return out;
}
}
