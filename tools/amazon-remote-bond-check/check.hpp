// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace arbond {
inline constexpr std::uint32_t NotAttempted=0xffffffff;
inline constexpr std::uint32_t InvalidSnapshot=0xfffffffe;
using Address=std::array<std::uint8_t,6>;
struct Query {
    std::uint32_t parameter_id{},parameter_result{NotAttempted},query_result{NotAttempted},
        returned_count{},target_matches{},invalid_count{};
};
struct Snapshot {
    std::uint32_t magic{0x41524231},version{1},open_result{NotAttempted},reserved{};
    std::uint64_t boot_ms{};
    std::array<Query,2> queries{{{1},{0xffff}}};
    std::uint32_t checksum{},reserved2{};
};
static_assert(sizeof(Query)==24 && sizeof(Snapshot)==80);
inline std::uint32_t Checksum(const Snapshot &s) {
    const auto *p=reinterpret_cast<const unsigned char *>(&s);std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<offsetof(Snapshot,checksum);++i)hash=(hash^p[i])*16777619u;
    return hash;
}
// Backend exposes only Open, system-parameter getter, paired-address getter,
// and Close. Never scans/connects/pairs/unpairs/reads keys or consumes events.
template<class Backend> Snapshot Collect(Backend &backend,const Address &target,std::uint64_t now) {
    Snapshot out{};out.boot_ms=now;out.open_result=backend.Open();
    if(!out.open_result) {
        for(auto &q:out.queries) {
            typename Backend::Parameter parameter{};
            q.parameter_result=backend.ParameterFor(q.parameter_id,parameter);
            if(q.parameter_result)continue;
            std::array<Address,10> addresses{};std::uint8_t count=0;
            q.query_result=backend.Paired(parameter,addresses,count);
            if(q.query_result)continue;
            q.returned_count=count;
            if(count>addresses.size()) {q.invalid_count=1;q.query_result=InvalidSnapshot;continue;}
            for(unsigned i=0;i<count;++i)q.target_matches+=addresses[i]==target;
            if(q.target_matches>1) {q.invalid_count=1;q.query_result=InvalidSnapshot;}
        }
        backend.Close();
    }
    out.checksum=Checksum(out);return out;
}
}
