// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "deferred_transport.hpp"
#include <array>
#include <limits>

namespace amazon_remote::deferred_read {
namespace t=deferred_transport;
inline constexpr std::uint32_t BeginCommand=65021,ReadCommand=65022,Magic=0x31535244;
inline constexpr std::uint64_t WindowMs=5000;
enum class State:std::uint32_t {Idle,Queued,Sending,Awaiting,Complete,NotReady,QueueFailed,SendFailed,TimedOut,ClockError};
struct Snapshot {
    std::uint32_t magic{Magic},version{1},size{144};State state{};
    std::uint64_t nonce{},started_ms{},deadline_ms{},finished_ms{};
    std::uint32_t send_finished{},send_result{},event_received{},rejected_events{};
    std::array<std::uint8_t,80> event{};
};
static_assert(sizeof(Snapshot)==144 && offsetof(Snapshot,event)==64);
inline bool Terminal(State s){return s>=State::Complete;}
inline bool Pending(State s){return s>=State::Queued && s<=State::Awaiting;}
inline bool Valid(const Snapshot &s) {
    if(s.magic!=Magic || s.version!=1 || s.size!=sizeof s || s.state>State::ClockError ||
       s.send_finished>1 || s.event_received>1 || (!s.send_finished && s.send_result))return false;
    if(s.event_received) {
        t::Event decoded{};
        if(!t::Decode(s.event.data(),s.event.size(),s.nonce,decoded))return false;
    } else for(auto b:s.event)if(b)return false;
    if(s.state==State::Idle)return !s.nonce && !s.started_ms && !s.deadline_ms && !s.finished_ms &&
        !s.send_finished && !s.send_result && !s.event_received && !s.rejected_events;
    if(!s.nonce || !s.started_ms || s.started_ms>std::numeric_limits<std::uint64_t>::max()-WindowMs ||
       s.deadline_ms!=s.started_ms+WindowMs)return false;
    if(Pending(s.state) && s.finished_ms)return false;
    if(Terminal(s.state) && s.state!=State::ClockError && s.finished_ms<s.started_ms)return false;
    switch(s.state) {
        case State::Queued:return !s.send_finished && !s.event_received && !s.rejected_events;
        case State::Sending:return !s.send_finished;
        case State::Awaiting:return s.send_finished && !s.send_result && !s.event_received;
        case State::Complete:return s.send_finished && !s.send_result && s.event_received && s.finished_ms<s.deadline_ms;
        case State::NotReady:case State::QueueFailed:
            return !s.send_finished && !s.event_received && !s.rejected_events;
        case State::SendFailed:return s.send_finished && s.send_result;
        case State::TimedOut:return s.finished_ms>=s.deadline_ms;
        case State::ClockError:return true;
        case State::Idle:break;
    }
    return false;
}

// One explicit read per companion lifetime. Serialize externally. No I/O, SDK,
// thread, allocation, retry, reset, pairing or radio operation in this model.
// A timeout freezes evidence, but cannot cancel an in-flight synchronous IPC.
class Session {
public:
    bool Begin(std::uint64_t nonce,std::uint64_t now,bool ready) {
        if(snapshot_.state!=State::Idle || !nonce || !now ||
           now>std::numeric_limits<std::uint64_t>::max()-WindowMs)return false;
        snapshot_.nonce=nonce;snapshot_.started_ms=now;snapshot_.deadline_ms=now+WindowMs;
        last_ms_=now;snapshot_.state=State::Queued;
        if(!ready)Finish(State::NotReady,now);
        return true;
    }
    bool TakeWork(std::uint64_t now,t::Request &out) {
        Tick(now);
        if(snapshot_.state!=State::Queued)return false;
        snapshot_.state=State::Sending;out=t::Request{t::RequestCode,t::RequestMagic,snapshot_.nonce};return true;
    }
    void QueueFailed(std::uint64_t now) {
        Tick(now);if(snapshot_.state==State::Queued)Finish(State::QueueFailed,now);
    }
    void Sent(std::uint32_t result,std::uint64_t now) {
        Tick(now);if(snapshot_.state!=State::Sending)return;
        snapshot_.send_finished=1;snapshot_.send_result=result;
        if(result)Finish(State::SendFailed,now);
        else if(snapshot_.event_received)Finish(State::Complete,now);
        else snapshot_.state=State::Awaiting;
    }
    bool Consume(std::uint32_t type,const void *data,std::size_t size,std::uint64_t now) {
        if(type!=t::EventType)return false;
        Tick(now);
        // Owned event types never enter ordinary Horizon processing, including
        // malformed, duplicate, unsolicited and late replies.
        if(snapshot_.state!=State::Sending && snapshot_.state!=State::Awaiting)return true;
        t::Event event{};
        if(snapshot_.event_received || !t::Decode(data,size,snapshot_.nonce,event)) {
            if(snapshot_.rejected_events!=std::numeric_limits<std::uint32_t>::max())++snapshot_.rejected_events;
            return true;
        }
        std::memcpy(snapshot_.event.data(),&event,sizeof event);snapshot_.event_received=1;
        if(snapshot_.state==State::Awaiting)Finish(State::Complete,now);
        return true;
    }
    Snapshot Read(std::uint64_t now){Tick(now);return snapshot_;}
private:
    void Finish(State state,std::uint64_t now){snapshot_.state=state;snapshot_.finished_ms=now;}
    void Tick(std::uint64_t now) {
        if(!Pending(snapshot_.state))return;
        if(now<last_ms_)Finish(State::ClockError,now);
        else if(now>=snapshot_.deadline_ms)Finish(State::TimedOut,now);
        last_ms_=now;
    }
    Snapshot snapshot_{};
    std::uint64_t last_ms_{};
};
}
