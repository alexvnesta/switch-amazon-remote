// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-pairing-audit/backend.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#ifdef POSTCHECK_TEST
#include "postcheck_fixture.hpp"
#endif
namespace {
const araudit::Address target{0x02,0xab,0xcd,0x12,0x34,0x56};
unsigned variant{},ticks{};std::vector<unsigned> calls;
void Address(BtdrvAddress &out){std::memcpy(out.address,target.data(),target.size());}
Result Connections(bool system,BtdrvBleConnectionInfo *out,u8 capacity,u8 *count) {
    calls.push_back(system?5:2);assert(capacity==16);Address(out[0].addr);out[0].connection_handle=4;
    *count=((system && (variant==11 || variant==13 || variant==25)) || (!system && (variant==12 || variant==13)))?0:1;
    if((!system && variant==3) || (system && variant==9))*count=16;
    if(!system && variant==4)*count=17;
    if(!system && variant==5){*count=2;Address(out[1].addr);out[1].connection_handle=5;}
    if(!system && variant==6)out[0].connection_handle=0;
    if(system && variant==10)out[0].connection_handle=5;
    if(system && variant==23)out[0].connection_handle=0xffffffff;
    if(system && variant==24){*count=2;out[1].addr.address[0]=1;out[1].connection_handle=4;}
    if(!system && variant==25)out[0].addr.address[0]=1;
    return (!system && variant==2)?52:(system && variant==8)?58:0;
}
Result Paired(bool system,BtdrvBleAdvertisePacketParameter p,BtdrvAddress *out,u8 capacity,u8 *count) {
    calls.push_back(system?6:3);assert(capacity==10 && p.company_id==0x171);
    const u8 pattern[]{4,0x27,2,0x7d,0,0};assert(!std::memcmp(p.pattern_data,pattern,6));
    Address(out[0]);*count=1;
    if((!system && variant==16) || (system && variant==26))*count=11;
    if((!system && variant==17) || (system && variant==27)){*count=2;Address(out[1]);}
    return (!system && variant==14)?64:(system && variant==15)?65:0;
}
}
u64 armGetSystemTick(){++ticks;return variant==31?0:ticks==1?1000:variant==20?999:variant==21?3001:variant==28?3000:1001;}
u64 armTicksToNs(u64 tick){return tick*1000000;}
Result btmuInitialize(){calls.push_back(1);return variant==1?51:0;}
void btmuExit(){calls.push_back(7);}
Result btmuBleGetConnectionState(BtdrvBleConnectionInfo *out,u8 count,u8 *total){return Connections(false,out,count,total);}
Result btmuBleGetPairedDevices(BtdrvBleAdvertisePacketParameter p,BtdrvAddress *out,u8 count,u8 *total){return Paired(false,p,out,count,total);}
Result btmInitialize(){calls.push_back(4);return variant==7?57:0;}
void btmExit(){calls.push_back(8);}
Result btmBleGetConnectionState(BtdrvBleConnectionInfo *out,u8 count,u8 *total){return Connections(true,out,count,total);}
Result btmBleGetPairedAddresses(BtdrvBleAdvertisePacketParameter p,BtdrvAddress *out,u8 count,u8 *total){return Paired(true,p,out,count,total);}
Result btmsysInitialize(){calls.push_back(9);return variant==18?68:0;}
void btmsysExit(){calls.push_back(11);}
Result btmsysIsGamepadPairingStarted(bool *started){calls.push_back(10);*started=variant!=22;return variant==19?69:0;}
int main(){
    for(variant=0;variant<32;++variant) {
        ticks=0;calls.clear();araudit::Backend b;
#ifdef POSTCHECK_TEST
        PostFixture f;arpostcheck::Collector collector;
        const auto record=collector.Once(b,target,f.owner,f.intent,f.status,1000,true);
        assert(arpostcheck::Valid(record) && record.guard==0);
        assert(arpostcheck::TimedAfterRequest(record)==(variant!=20 && variant!=21 && variant!=31));
        const auto previous=calls;
        const auto second=collector.Once(b,target,f.owner,f.intent,f.status,1000,true);
        assert(second.guard==unsigned(arpostcheck::Guard::Preflight) && calls==previous && arpostcheck::Valid(second));
        arpostcheck::Collector refused;f.status.state=unsigned(armodule::State::Ready);f.Seal();
        const auto bad=refused.Once(b,target,f.owner,f.intent,f.status,1000,true);
        assert(bad.guard==unsigned(arpostcheck::Guard::Status) && calls==previous && arpostcheck::Valid(bad));
        arpostcheck::Collector preflight;
        assert(preflight.Once(b,target,f.owner,f.intent,f.status,1000,false).guard==unsigned(arpostcheck::Guard::Preflight) && calls==previous);
        const auto s=record.snapshot;
#else
        const auto s=araudit::CollectContext(b,target);
#endif
        assert(s.magic==0x41525033 && s.version==3 && !s.reserved && !s.reserved0 && s.checksum==araudit::Checksum(s) && ticks==2);
        std::vector<unsigned> expected=variant==1?std::vector<unsigned>{1}:std::vector<unsigned>{1,2,3,7};
        for(unsigned c:variant==7?std::vector<unsigned>{4}:std::vector<unsigned>{4,5,6,8})expected.push_back(c);
        for(unsigned c:variant==18?std::vector<unsigned>{9}:std::vector<unsigned>{9,10,11})expected.push_back(c);
        assert(calls==expected); // Actual SDK adapter has getters only; every successful client closes once.
        using Relation=araudit::ConnectionRelation;auto relation=Relation::SameHandle;
        if(variant==1 || variant==2 || variant==3 || variant==4 || variant==5 || variant==6 || variant==7 || variant==8 || variant==9 ||
           variant==20 || variant==21 || variant==23 || variant==24 || variant==31)relation=Relation::Indeterminate;
        else if(variant==10)relation=Relation::DifferentHandle;
        else if(variant==11)relation=Relation::UserOnly;
        else if(variant==12)relation=Relation::SystemOnly;
        else if(variant==13 || variant==25)relation=Relation::Neither;
        assert(araudit::Compare(s)==relation);
        if(variant==1)assert(s.user_connections.list.result==araudit::NotAttempted && s.user_paired.result==araudit::NotAttempted);
        if(variant==7)assert(s.system_connections.list.result==araudit::NotAttempted && s.system_paired.result==araudit::NotAttempted);
        if(variant==2)assert(s.user_connections.list.result==52 && !s.user_connections.list.count && !s.user_connections.target_handle);
        if(variant==8)assert(s.system_connections.list.result==58 && !s.system_connections.list.count && !s.system_connections.target_handle);
        if(variant==14)assert(s.user_paired.result==64 && !s.user_paired.count && !s.user_paired.matches);
        if(variant==15)assert(s.system_paired.result==65 && !s.system_paired.count && !s.system_paired.matches);
        if(variant==16 || variant==17)assert(s.user_paired.result==araudit::InvalidSnapshot && s.user_paired.invalid);
        if(variant==26 || variant==27)assert(s.system_paired.result==araudit::InvalidSnapshot && s.system_paired.invalid);
        if(variant==18)assert(s.pairing_query==araudit::NotAttempted && !s.pairing_started);
        else if(variant==19)assert(s.pairing_query==69 && !s.pairing_started);
        else assert(!s.pairing_query && s.pairing_started==(variant!=22));
        // The target address never enters the address-free persistence record.
        const auto *bytes=reinterpret_cast<const unsigned char *>(&s);
        bool leaked=false;for(unsigned i=0;i+target.size()<=sizeof s;++i)leaked|=!std::memcmp(bytes+i,target.data(),target.size());
        assert(!leaked);
    }
    std::puts("PASS read-only context:32 actual-SDK adapter scenarios, client cleanup, list bounds/duplicates/handles, independent failures/timing; Post1 adds one-shot/refused/preflight no-call checks when enabled. NOT hardware proof.");
}
