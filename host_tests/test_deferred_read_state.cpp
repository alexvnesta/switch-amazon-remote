// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/driver-capability-p1/deferred_read_state.hpp"
#include <cassert>
#include <cstdio>
namespace d=amazon_remote::deferred_capability;
namespace t=amazon_remote::deferred_transport;
namespace s=amazon_remote::deferred_read;
constexpr std::uint64_t Nonce=0x12345678abcdef01;
t::Event Empty(){t::Event e{};e.nonce=Nonce;e.record.event.magic=e.record.event.version=0;return e;}
bool Same(const s::Snapshot &a,const s::Snapshot &b){return !std::memcmp(&a,&b,sizeof a);}
s::Snapshot Read(s::Session &session,std::uint64_t now) {
    const auto value=session.Read(now);assert(s::Valid(value));return value;
}
int main() {
    const auto event=Empty();t::Event decoded{};
    assert(t::Decode(&event,sizeof event,Nonce,decoded));
    const auto original=decoded;
    for(std::size_t size=0;size<160;++size)if(size!=sizeof event) {
        assert(!t::Decode(&event,size,Nonce,decoded) && !std::memcmp(&original,&decoded,sizeof decoded));
    }
    assert(!t::Decode(nullptr,80,Nonce,decoded));assert(!t::Decode(&event,80,0,decoded));
    std::uint8_t unaligned[81]{};std::memcpy(unaligned+1,&event,sizeof event);
    assert(t::Decode(unaligned+1,80,Nonce,decoded));
    for(unsigned index=0;index<80;++index) {
        auto bad=event;reinterpret_cast<std::uint8_t*>(&bad)[index]^=0x80;
        assert(!t::Decode(&bad,sizeof bad,Nonce,decoded));
    }
    s::Session idle;assert(Read(idle,100).state==s::State::Idle);
    assert(!idle.Begin(0,100,true) && !idle.Begin(Nonce,0,true));
    assert(!idle.Begin(Nonce,~std::uint64_t{},true));
    assert(idle.Consume(t::EventType,&event,80,100));assert(Read(idle,100).state==s::State::Idle);
    for(bool event_first:{false,true})for(unsigned result:{0u,0xf601u}) {
        s::Session session;t::Request request{};
        assert(session.Begin(Nonce,100,true));assert(!session.Begin(Nonce+1,101,true));
        assert(session.TakeWork(101,request) && request.command==11 && request.nonce==Nonce);
        assert(!session.TakeWork(102,request));
        if(event_first)assert(session.Consume(t::EventType,&event,80,102));
        session.Sent(result,103);
        if(!event_first)assert(session.Consume(t::EventType,&event,80,104));
        const auto done=Read(session,105);
        assert(done.state==(result?s::State::SendFailed:s::State::Complete));
        assert(done.event_received==(!result || event_first));
        assert(!session.Begin(Nonce,106,true));session.Sent(0,106);session.QueueFailed(106);
        assert(session.Consume(t::EventType,&event,80,107));assert(Same(done,Read(session,10000)));
    }
    for(unsigned stage=0;stage<4;++stage) {
        s::Session session;t::Request request{};assert(session.Begin(Nonce,100,true));
        if(stage){assert(session.TakeWork(101,request));if(stage==2)session.Sent(0,102);
            if(stage==3)assert(session.Consume(t::EventType,&event,80,102));}
        const auto timed=Read(session,5100);assert(timed.state==s::State::TimedOut);
        assert(!session.TakeWork(5101,request));session.Sent(0,5101);
        assert(session.Consume(t::EventType,&event,80,5101));assert(Same(timed,Read(session,9999)));
    }
    for(auto state:{s::State::NotReady,s::State::QueueFailed,s::State::ClockError}) {
        s::Session session;assert(session.Begin(Nonce,100,state!=s::State::NotReady));
        if(state==s::State::QueueFailed)session.QueueFailed(101);
        const auto snapshot=Read(session,state==s::State::ClockError?99:102);
        assert(snapshot.state==state);assert(!session.Begin(Nonce+1,200,true));
    }
    s::Session pending;t::Request request{};assert(pending.Begin(Nonce,100,true));
    assert(pending.TakeWork(101,request));auto bad=event;bad.nonce++;
    for(unsigned i=0;i<100;++i)assert(pending.Consume(t::EventType,&bad,80,102));
    assert(!pending.Consume(0x6c,&event,80,102));assert(Read(pending,102).rejected_events==100);
    assert(pending.Consume(t::EventType,&event,80,103));
    assert(pending.Consume(t::EventType,&bad,80,104));
    pending.Sent(0,105);const auto complete=Read(pending,106);
    assert(complete.state==s::State::Complete && complete.rejected_events==101);
    assert(!std::memcmp(complete.event.data(),&event,sizeof event));
    for(unsigned index=0;index<16;++index) {
        auto mutated=complete;reinterpret_cast<std::uint8_t*>(&mutated)[index]^=0x80;
        assert(!s::Valid(mutated));
    }
    std::puts("PASS fixed metadata wire and one-shot late read: event-before-send-return, failures, timeout freeze, readiness, nonce, no retry");
}
