// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/controller_passive_cache.hpp"
#include "../mc_mitm/source/amazon_remote/controller_passive_runtime.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <cstdio>
namespace p=amazon_remote::controller_passive;
namespace runtime=ams::mc::controller_passive_runtime;
std::atomic<std::uint64_t> now{100};
std::uint64_t armGetSystemTick(){return now.load()*1000000;}
int main(){
    p::Event event{};event.parameter_length=event.copied=9;event.prefix={0,8,0,0x34,0x12,0,1,16,0};
    p::Cache cache;assert(!cache.Start(0) && cache.Start(100) && !cache.Start(200));
    assert(p::Valid(cache.Read()) && !cache.Read().accepted);
    cache.Observe(&event,sizeof event,101);auto first=cache.Read();
    assert(p::Valid(first) && first.first.max_filters==16 && first.accepted==1);
    cache.Observe(&event,sizeof event,102);assert(!cache.Read().conflicting);
    event.prefix[7]=17;cache.Observe(&event,sizeof event,103);
    assert(p::Valid(cache.Read()) && cache.Read().conflicting && cache.Read().first.max_filters==16);
    assert(p::SummaryReply(cache.Read())==amazon_remote::controller_capabilities::Reply{});
    cache.Observe(&event,sizeof event,99);assert(cache.Read().clock_error && cache.Read().rejected==1);
    cache.Observe(&event,sizeof event,104);assert(cache.Read().rejected==2); // Clock failure is latched.
    for(unsigned i=5;i<65;++i)cache.Observe(nullptr,0,105);
    auto limited=cache.Read();assert(p::Valid(limited) && limited.attempts==64 && limited.limit_reached);
    cache.Observe(reinterpret_cast<void*>(1),10000,106); // No dereference after lifetime bound.
    for(unsigned member=0;member<8;++member){
        auto bad=first;
        if(member==0){bad.magic^=1;}if(member==1){bad.version=2;}if(member==2){bad.reserved0=1;}
        if(member==3){bad.accepted=65;}if(member==4){bad.rejected=2;}if(member==5){bad.first_ms=99;}
        if(member==6){bad.last_ms=100;}if(member==7){bad.first.max_filters=256;}
        assert(!p::Valid(bad));
    }
    p::Cache malformed;assert(malformed.Start(100));event.version=2;
    malformed.Observe(&event,sizeof event,100);assert(p::Valid(malformed.Read()) && malformed.Read().rejected==1);
    event.version=1;
    p::Snapshot wire{};assert(runtime::Read(&wire,sizeof wire) && p::Valid(wire) && !wire.attempts);
    assert(!runtime::Consume(1,reinterpret_cast<void*>(1),10000));
    assert(!runtime::Read(nullptr,sizeof wire) && !runtime::Read(&wire,sizeof wire-1));
    assert(runtime::Consume(p::EventType,nullptr,0)); // Malformed private event must not be forwarded.
    std::atomic<bool> finished{};
    std::thread producer([&]{for(unsigned i=0;i<1000;++i){now.fetch_add(1);assert(runtime::Consume(p::EventType,&event,sizeof event));}finished=true;});
    std::thread reader([&]{do{p::Snapshot current{};assert(runtime::Read(&current,sizeof current) && p::Valid(current));}while(!finished);});
    producer.join();reader.join();assert(runtime::Read(&wire,sizeof wire));
    assert(wire.attempts==64 && wire.accepted==63 && wire.rejected==1 && wire.limit_reached);
    std::puts("PASS passive cache/runtime: historical metadata, conflicts, malformed traffic, clock/limit latch, serialized readers, no Bluetooth dependencies");
}
