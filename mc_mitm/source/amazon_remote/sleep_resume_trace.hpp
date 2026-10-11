// SPDX-License-Identifier: GPL-2.0-only
// Passive metadata only. No driver calls, peer bytes, keys or wake policy.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "intent_trace.hpp"
namespace amazon_remote::sleep_trace {
inline constexpr std::uint64_t WindowMs=600000, AfterResumeMs=10000;
enum class Kind : std::uint32_t { Request=1, Acknowledge, BleHeader, Intent };
enum class Verdict : std::uint32_t { Pending, SleepResumeObserved, Expired, PowerError, Shutdown };
struct Record {
    std::uint64_t boot_ms{},program{};
    std::uint32_t sequence{},kind{},selector{},result{},flags{},reported_size{};
    std::array<std::uint8_t,8> metadata{};
};
static_assert(sizeof(Record)==48);
template<std::size_t N> class Queue {
public:
    void Push(Record record) {
        record.sequence=seen_.fetch_add(1,std::memory_order_relaxed);
        const auto head=head_.load(std::memory_order_relaxed);
        if(head-tail_.load(std::memory_order_acquire)>=N) {drops_.fetch_add(1,std::memory_order_relaxed);return;}
        data_[head%N]=record;head_.store(head+1,std::memory_order_release);
    }
    bool Pop(Record &out) {
        const auto tail=tail_.load(std::memory_order_relaxed);
        if(tail==head_.load(std::memory_order_acquire))return false;
        out=data_[tail%N];tail_.store(tail+1,std::memory_order_release);return true;
    }
    std::uint32_t Drops()const{return drops_.load(std::memory_order_relaxed);}
private:
    std::array<Record,N> data_{};
    std::atomic<std::uint32_t> head_{},tail_{},seen_{},drops_{};
};
struct Report {
    std::uint32_t magic{0x41525331},version{1},bytes{sizeof(Report)},verdict{},
        power_count{},intent_count{},ble_count{},power_drops{},intent_drops{},ble_drops{},
        flags{7},checksum{}; // passive, ownership unproven, late counters approximate
    std::uint64_t begin_ms{},deadline_ms{},sleep_ms{},resume_ms{},finished_ms{};
    std::array<std::uint32_t,3> intent_phase_drops{},ble_phase_drops{};
    std::array<Record,32> power{};
    std::array<Record,192> intents{},ble{}; //64 slots reserved independently per phase.
};
static_assert(sizeof(Report)==20080 && offsetof(Report,checksum)==44);
inline std::uint32_t Checksum(const Report &r) {
    auto *p=reinterpret_cast<const std::uint8_t *>(&r);std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<sizeof r;++i)h=(h^((i>=44&&i<48)?0:p[i]))*16777619u;
    return h;
}
inline bool Valid(const Report &r) {
    if(r.magic!=0x41525331 || r.version!=1 || r.bytes!=sizeof r || r.flags!=7 ||
       r.verdict<1 || r.verdict>4 || r.power_count>32 || r.intent_count>192 || r.ble_count>192 ||
       !r.begin_ms || r.begin_ms>UINT64_MAX-WindowMs || r.deadline_ms!=r.begin_ms+WindowMs ||
       r.finished_ms<r.begin_ms || r.checksum!=Checksum(r))return false;
    if(r.verdict==unsigned(Verdict::SleepResumeObserved) &&
       (!r.sleep_ms || r.sleep_ms<r.begin_ms || r.resume_ms<r.sleep_ms ||
        r.resume_ms>UINT64_MAX-AfterResumeMs || r.finished_ms<r.resume_ms+AfterResumeMs))return false;
    return true;
}
class Trace {
public:
    // Startup only. Immutable before any producer starts. No rearm after stop.
    void Start(bool opted_in,std::uint64_t now) {
        if(initialized_)return;
        initialized_=true;
        if(!opted_in || !now || now>UINT64_MAX-WindowMs)return;
        begin_=now;deadline_=now+WindowMs;active_.store(true,std::memory_order_release);
    }
    bool Live(std::uint64_t now)const {
        return active_.load(std::memory_order_acquire) && now>=begin_ && now<deadline_;
    }
    // Sole IPC producer. Never retain AD/UUID/address/filter mask contents.
    void Intent(std::uint32_t command,std::uint64_t program,const void *data,std::size_t size,std::uint64_t now) {
        if(!Live(now) || (program!=HidProgram && program!=BtmProgram) ||
           !((command>=47&&command<=49)||(command>=55&&command<=61)))return;
        Record r{};r.boot_ms=now;r.program=program;r.kind=unsigned(Kind::Intent);r.selector=command;
        r.result=UINT32_MAX;r.reported_size=size>UINT32_MAX?UINT32_MAX:std::uint32_t(size);
        const auto expected=(command==57||command==58)?62u:(command==59||command==61)?1u:0u;
        if(size!=expected)r.flags|=1;
        if(!data && size)r.flags|=2;
        if(data && size==expected) {
            const auto *p=static_cast<const std::uint8_t *>(data);
            if(expected==62){r.metadata[0]=p[0];r.metadata[1]=p[1];r.metadata[2]=p[2];r.metadata[3]=p[61];}
            else if(expected==1)r.metadata[0]=p[0];
        }
        const auto phase=phase_.load(std::memory_order_acquire);r.flags|=phase<<24;
        intents_[phase].Push(r);
    }
    // Sole BLE producer. Copies only pre-address or result/action bytes.
    void Ble(std::uint32_t type,const void *data,std::size_t size,std::uint64_t now) {
        if(!Live(now) || (type!=6&&type!=7))return;
        Record r{};r.boot_ms=now;r.kind=unsigned(Kind::BleHeader);r.selector=type;
        r.reported_size=size>UINT32_MAX?UINT32_MAX:std::uint32_t(size);
        const auto bound=type==6?7u:8u;
        const auto copied=data?(size<bound?size:bound):0;
        r.flags=std::uint32_t(copied);if(copied)std::memcpy(r.metadata.data(),data,copied);
        const auto phase=phase_.load(std::memory_order_acquire);r.flags|=phase<<24;
        ble_[phase].Push(r);
    }
    // PSC requests, acknowledgements and persistence run on the SAME Main thread.
    // This observer never calls the existing power-policy adapter or radio.
    void Request(std::uint32_t state,std::uint32_t flags,std::uint32_t result,std::uint64_t now) {
        if(!begin_ || finished_)return;
        can_save_=false;pending_=true;pending_state_=state;pending_result_=result;
        if(now<begin_){Fail(Verdict::PowerError);pending_result_=1;return;}
        previous_=last_state_;pending_flags_=flags;
        Record r{};r.boot_ms=now;r.kind=unsigned(Kind::Request);r.selector=state;r.result=result;r.flags=flags;
        std::memcpy(r.metadata.data(),&previous_,4);power_.Push(r);
        if(result || state>5)Fail(Verdict::PowerError);
        else if(state==5)Fail(Verdict::Shutdown);
        else {
            last_state_=state;
            if((state==2||state==3) && !sleep_){sleep_=now;phase_.store(1,std::memory_order_release);}
        }
    }
    void Acknowledge(std::uint32_t result,std::uint64_t now) {
        if(!pending_ || finished_)return;
        pending_=false;
        if(now<begin_){can_save_=false;Fail(Verdict::PowerError);return;}
        Record r{};r.boot_ms=now;r.kind=unsigned(Kind::Acknowledge);r.selector=pending_state_;
        r.result=result;r.flags=pending_flags_;power_.Push(r);
        if(result || pending_result_) {Fail(Verdict::PowerError);return;}
        const bool awake=pending_state_==0 || (pending_state_==1 && (previous_==4||previous_==0||previous_==1));
        if(awake) {
            can_save_=true;
            if(sleep_ && !resume_){resume_=now;phase_.store(2,std::memory_order_release);} // First resume only, never renew.
        }
    }
    bool Ready(std::uint64_t now) {
        if(!begin_ || finished_ || now<begin_)return false;
        if(now>=deadline_ && verdict_==Verdict::Pending){verdict_=Verdict::Expired;active_.store(false,std::memory_order_release);}
        if(!can_save_)return false;
        if(verdict_!=Verdict::Pending)return true;
        return resume_ && resume_<=UINT64_MAX-AfterResumeMs && now>=resume_+AfterResumeMs;
    }
    bool Finish(Report &out,std::uint64_t now) {
        if(!Ready(now))return false;
        active_.store(false,std::memory_order_release);finished_=true;
        out={};out.begin_ms=begin_;out.deadline_ms=deadline_;out.sleep_ms=sleep_;out.resume_ms=resume_;out.finished_ms=now;
        out.verdict=unsigned(verdict_==Verdict::Pending?Verdict::SleepResumeObserved:verdict_);
        while(out.power_count<out.power.size() && power_.Pop(out.power[out.power_count]))++out.power_count;
        out.power_drops=power_.Drops();
        for(unsigned phase=0;phase<3;++phase) {
            unsigned count{};
            while(count<64 && intents_[phase].Pop(out.intents[out.intent_count])){++out.intent_count;++count;}
            count=0;
            while(count<64 && ble_[phase].Pop(out.ble[out.ble_count])){++out.ble_count;++count;}
            out.intent_phase_drops[phase]=intents_[phase].Drops();out.ble_phase_drops[phase]=ble_[phase].Drops();
            out.intent_drops+=out.intent_phase_drops[phase];out.ble_drops+=out.ble_phase_drops[phase];
        }
        out.checksum=Checksum(out);return true;
    }
private:
    void Fail(Verdict why){verdict_=why;active_.store(false,std::memory_order_release);}
    Queue<32> power_;std::array<Queue<64>,3> intents_,ble_;
    std::atomic<bool> active_{};
    std::atomic<std::uint32_t> phase_{}; // PSC-declared phase, not hardware power state.
    std::uint64_t begin_{},deadline_{},sleep_{},resume_{};
    bool initialized_{},pending_{},can_save_{},finished_{};
    std::uint32_t previous_{6},last_state_{6},pending_state_{6},pending_result_{},pending_flags_{};
    Verdict verdict_{Verdict::Pending};
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
}
