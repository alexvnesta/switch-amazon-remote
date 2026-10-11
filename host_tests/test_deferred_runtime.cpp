// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include "../mc_mitm/source/amazon_remote/deferred_read_runtime.hpp"
#include "../tools/driver-capability-p1/deferred_read_state.hpp"
#include <functional>
#include <memory>
#include <string>
#include <cstdio>
namespace dr=ams::mc::deferred_read_runtime;
namespace d=amazon_remote::deferred_read;
namespace t=amazon_remote::deferred_transport;
std::uint64_t test_now_ms=100;
bool session_ready=true,event_ready=true,queue_ready=true;
unsigned sends=0,queues=0;
std::string scenario;
std::unique_ptr<std::function<Result()>> pending;
t::Event reply{};
namespace ams::mitm {bool IsInitialized(){return session_ready;}}
namespace ams::bluetooth::events {bool IsReady(){return event_ready;}}
namespace ams::async {
bool TryQueueWork(std::function<Result()> *function) {
    ++queues;if(!queue_ready)return false;assert(!pending);pending.reset(function);return true;
}
}
Result btdrvWriteHidData2(BtdrvAddress address,const void *bytes,std::size_t size) {
    ++sends;for(auto b:address.address)assert(!b);assert(size==16);
    t::Request request{};std::memcpy(&request,bytes,size);
    assert(request.command==11 && request.magic==t::RequestMagic && request.nonce==1234);
    if(scenario=="reply-before")assert(dr::Consume(t::EventType,&reply,80));
    if(scenario=="timeout-native")test_now_ms=5200;
    return scenario=="error"?0xf601:0;
}
d::Snapshot Read(){d::Snapshot out;assert(dr::Read(&out,sizeof out));assert(d::Valid(out));return out;}
int main(int argc,char **argv) {
    assert(argc==2);scenario=argv[1];
    if(scenario=="not-ready")session_ready=false;
    if(scenario=="no-events")event_ready=false;
    if(scenario=="queue-full")queue_ready=false;
    reply.nonce=1234;reply.record.event.magic=reply.record.event.version=0;
    assert(Read().state==d::State::Idle);assert(!dr::Begin(0));
    assert(!dr::Read(nullptr,sizeof(d::Snapshot)));
    assert(!dr::Consume(0x6c,&reply,80));
    assert(dr::Begin(1234));assert(!dr::Begin(1234));
    if(!session_ready || !event_ready){assert(Read().state==d::State::NotReady && !queues && !sends);return 0;}
    if(!queue_ready){assert(Read().state==d::State::QueueFailed && !pending && !sends);return 0;}
    assert(pending && queues==1);
    if(scenario=="timeout-queued")test_now_ms=5200;
    assert((*pending)()==0);pending.reset();
    if(scenario=="timeout-queued"){assert(Read().state==d::State::TimedOut && !sends);return 0;}
    assert(sends==1);
    if(scenario=="error"){assert(Read().state==d::State::SendFailed);return 0;}
    if(scenario=="timeout-native"){assert(Read().state==d::State::TimedOut);return 0;}
    if(scenario=="reply-before"){assert(Read().state==d::State::Complete);return 0;}
    assert(scenario=="reply-after");assert(Read().state==d::State::Awaiting);
    // API capacity is larger than the fixed native reply. The runtime must
    // decode exactly 80, not mistake the full 1024-byte capacity for wire size.
    std::uint8_t buffer[1024]{};std::memcpy(buffer,&reply,80);
    assert(dr::Consume(t::EventType,buffer,79));assert(Read().rejected_events==1);
    assert(dr::Consume(t::EventType,buffer,sizeof buffer));assert(Read().state==d::State::Complete);
    assert(dr::Consume(t::EventType,nullptr,0));assert(Read().state==d::State::Complete && sends==1);
    std::puts("PASS actual runtime: fixed request, no lock across native call, capacity decoding, one shot");
}
