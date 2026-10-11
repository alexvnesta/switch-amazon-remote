// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/driver-capability-p1/deferred_capture.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
namespace d=amazon_remote::deferred_capability;
namespace p=amazon_remote::controller_passive;
namespace c=amazon_remote::controller_capabilities;
int main(){
    const std::uint8_t prefix[9]={0,8,0,0x34,0x12,0,1,16,0};
    const auto *unreadable=reinterpret_cast<const std::uint8_t*>(1);
    for(unsigned length=0;length<=65535;++length){
        d::Slot slot;p::Event event{};event.reserved=123;
        assert(slot.ReadFirst(event)==d::Read::Empty && event.reserved==123);
        assert(slot.TryCapture(1,unreadable,length)==d::Capture::Ignored);
        // No competing writer. Weak CAS may spuriously fail, which is a valid
        // drop, never a reason for production code to retry.
        auto result=slot.TryCapture(c::VendorCapabilities,prefix,length);
        if(result==d::Capture::NotClaimed){assert(slot.ReadFirst(event)==d::Read::Empty);continue;}
        const bool complete=length>=9 && length!=10 && length<=252;
        if(!complete){assert(result==d::Capture::Rejected && slot.ReadFirst(event)==d::Read::Empty);continue;}
        assert(result==d::Capture::Published && slot.ReadFirst(event)==d::Read::Ready);
        c::Reply reply{};assert(p::Decode(&event,sizeof event,reply));
        assert(reply.outcome==c::Outcome::Reported);
        const auto before=event;
        assert(slot.TryCapture(c::VendorCapabilities,unreadable,9)==d::Capture::NotClaimed);
        assert(slot.ReadFirst(event)==d::Read::Ready && !std::memcmp(&before,&event,sizeof event));
    }
    for(unsigned status=1;status<=255;++status){
        d::Slot slot;const auto byte=std::uint8_t(status);p::Event event{};c::Reply reply{};
        if(slot.TryCapture(c::VendorCapabilities,&byte,252)==d::Capture::NotClaimed)continue;
        assert(slot.ReadFirst(event)==d::Read::Ready && event.copied==1);
        assert(p::Decode(&event,sizeof event,reply) && reply.outcome==c::Outcome::ControllerError && reply.status==status);
    }
    {d::Slot slot;p::Event event{};
     assert(slot.TryCapture(c::VendorCapabilities,nullptr,9)!=d::Capture::Published);
     assert(slot.ReadFirst(event)==d::Read::Empty);
     const std::uint8_t success_status=0;
     assert(slot.TryCapture(c::VendorCapabilities,&success_status,1)!=d::Capture::Published);
     assert(slot.ReadFirst(event)==d::Read::Empty);
     std::uint8_t bad[9];std::memcpy(bad,prefix,9);bad[6]=2;
     assert(slot.TryCapture(c::VendorCapabilities,bad,9)!=d::Capture::Published);
     assert(slot.ReadFirst(event)==d::Read::Empty);
     const auto final=slot.TryCapture(c::VendorCapabilities,prefix,9);
     assert((slot.ReadFirst(event)==d::Read::Ready)==(final==d::Capture::Published));
    }
    for(unsigned trial=0;trial<128;++trial){
        d::Slot slot;std::atomic<unsigned> successes{},finished{};std::atomic<bool> start{};
        std::vector<std::thread> workers;
        for(unsigned index=0;index<4;++index)workers.emplace_back([&,index]{
            while(!start.load())std::this_thread::yield();
            std::uint8_t local[9];std::memcpy(local,prefix,9);local[7]=index+1;
            if(slot.TryCapture(c::VendorCapabilities,local,9)==d::Capture::Published)++successes;
            ++finished;
        });
        workers.emplace_back([&]{
            start=true;
            do{p::Event event{};if(slot.ReadFirst(event)==d::Read::Ready){
                c::Reply reply{};assert(p::Decode(&event,sizeof event,reply));
                assert(reply.outcome==c::Outcome::Reported && reply.max_filters>=1 && reply.max_filters<=4);
            }}while(finished.load()!=4);
        });
        for(auto &thread:workers)thread.join();
        assert(successes<=1); // All weak claims may fail. Absence is an allowed result.
        p::Event event{};assert((slot.ReadFirst(event)==d::Read::Ready)==(successes==1));
    }
    std::puts("PASS deferred capture: all 65536 lengths, one-byte errors, null/unselected payloads, immutable first record, concurrent readers/writers; no native integration or hardware proof");
}
