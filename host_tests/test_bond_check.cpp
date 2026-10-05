// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-bond-check/check.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace arbond;
namespace {
const Address target{0x02,0xab,0xcd,0x12,0x34,0x56};
struct Backend {
    using Parameter=unsigned;
    unsigned variant{},opens{},closes{},getters{},queries{};
    std::uint32_t Open() {++opens;return variant==1?47:0;}
    std::uint32_t ParameterFor(unsigned id,Parameter &p) {++getters;assert(id==1 || id==0xffff);p=id;return variant==2?48:0;}
    std::uint32_t Paired(Parameter p,std::array<Address,10> &addresses,std::uint8_t &count) {
        ++queries;assert(p==1 || p==0xffff);
        if(variant==3)return 49;
        if(variant==4){count=11;return 0;}
        count=variant==5?2:variant==6?10:variant==7?0:1;
        if(count)addresses[0]=target;
        if(variant==5)addresses[1]=target;
        return 0;
    }
    void Close() {++closes;}
};
}
int main() {
    for(unsigned variant=0;variant<8;++variant) {
        Backend backend;backend.variant=variant;const auto s=Collect(backend,target,1234);
        assert(s.magic==0x41524231 && s.version==1 && s.boot_ms==1234 && !s.reserved && !s.reserved2 && s.checksum==Checksum(s));
        assert(backend.opens==1 && backend.closes==(variant!=1));
        for(auto q:s.queries) {
            if(variant==1)assert(s.open_result==47 && q.parameter_result==NotAttempted && q.query_result==NotAttempted && !backend.getters && !backend.queries);
            else if(variant==2)assert(q.parameter_result==48 && q.query_result==NotAttempted && !backend.queries);
            else if(variant==3)assert(q.query_result==49 && !q.target_matches);
            else if(variant==4 || variant==5)assert(q.query_result==InvalidSnapshot && q.invalid_count);
            else assert(!q.query_result && !q.invalid_count && q.target_matches==(variant!=7));
        }
    }
    std::puts("PASS one-shot getter-only paired-list collector: open/get/query failures, count0/10/overflow, duplicate target, cleanup, no addresses in persisted record. MODEL ONLY.");
}
