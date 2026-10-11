// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/controller_passive.hpp"
#include <cassert>
#include <cstdio>
int main(){
    namespace p=amazon_remote::controller_passive;
    namespace c=amazon_remote::controller_capabilities;
    p::Event event{};c::Reply reply{};
    assert(p::Decode(&event,sizeof event,reply) && reply.outcome==c::Outcome::Malformed);
    event.parameter_length=9;event.copied=9;event.prefix={0,8,0,0x34,0x12,0,1,16,0};
    assert(p::Decode(&event,sizeof event,reply) && reply.outcome==c::Outcome::Reported &&
           reply.max_filters==16 && reply.scan_storage_bytes==0x1234);
    event.parameter_length=252;
    assert(p::Decode(&event,sizeof event,reply) && reply.uninterpreted_extension_bytes==243);
    for(unsigned length=0;length<=65535;++length){
        auto other=event;other.parameter_length=length;
        assert(p::Decode(&other,sizeof other,reply)==(length==9 || (length>=11 && length<=252)));
    }
    event.copied=1;event.prefix={17};
    assert(p::Decode(&event,sizeof event,reply) && reply.outcome==c::Outcome::ControllerError && reply.status==17);
    for(unsigned i=1;i<12;++i){auto other=event;other.prefix[i]=1;assert(!p::Decode(&other,sizeof other,reply));}
    for(auto field:{&p::Event::magic,&p::Event::version,&p::Event::reserved}){
        auto other=event;other.*field^=1;assert(!p::Decode(&other,sizeof other,reply));
    }
    event.copied=0;event.prefix={};event.parameter_length=65535;
    assert(p::Decode(&event,sizeof event,reply) && reply.outcome==c::Outcome::Malformed);
    for(unsigned copied=2;copied<32;++copied){
        if(copied==9)continue;
        auto other=event;other.copied=copied;assert(!p::Decode(&other,sizeof other,reply));
    }
    assert(!p::Decode(nullptr,sizeof event,reply) && !p::Decode(&event,sizeof event-1,reply));
    std::puts("PASS passive capability event decoder: strict schema/prefix/tail, all native lengths, no transport");
}
