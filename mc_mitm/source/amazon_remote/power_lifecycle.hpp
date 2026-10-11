// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "capture_diagnostics.hpp"
#include <array>
#include <cstring>

namespace amazon_remote {
// Native PSC values. The SDK integration statically checks all six values.
enum class PowerState : std::uint32_t { FullAwake, MinimumAwake, SleepReady,
    EssentialSleepReady, EssentialAwake, ShutdownReady, Unknown };
enum class PowerGate : std::uint32_t { Open, PausedUnused, Terminal, PausedSession };
struct PowerEvent {
    std::uint64_t boot_ms{};
    std::uint32_t state{}, previous{}, flags{}, get_result{}, ack_result{UINT32_MAX}, gate{};
};
struct PowerHistoryWire {
    std::uint32_t magic{0x41525057}, version{1}, sequence{}, count{}, total{},
        last_state{unsigned(PowerState::Unknown)}, gate{}, reserved{};
    std::uint64_t now_boot_ms{};
    std::array<PowerEvent,16> events{}; // Oldest to newest, bounded ring snapshot.
};
static_assert(sizeof(PowerEvent)==32 && sizeof(PowerHistoryWire)==552);
static_assert(std::is_trivially_copyable_v<PowerHistoryWire>);

// Single PSC writer, concurrent metadata getters. Every payload word is atomic;
// the sequence check is not a seqlock over non-atomic C++ objects.
class PowerHistory {
public:
    void Publish(const PowerHistoryWire &wire) {
        serial_.fetch_add(1,std::memory_order_acq_rel);
        for(unsigned i=0;i<words_.size();++i) {
            std::uint64_t word{};std::memcpy(&word,reinterpret_cast<const unsigned char*>(&wire)+i*8,8);
            words_[i].store(word,std::memory_order_release);
        }
        serial_.fetch_add(1,std::memory_order_release);
    }
    bool Read(PowerHistoryWire &wire) const {
        for(unsigned retry=0;retry<3;++retry) {
            const auto before=serial_.load(std::memory_order_acquire);
            if(!before || (before&1))continue;
            PowerHistoryWire next{};
            for(unsigned i=0;i<words_.size();++i) {
                const auto word=words_[i].load(std::memory_order_acquire);
                std::memcpy(reinterpret_cast<unsigned char*>(&next)+i*8,&word,8);
            }
            if(before==serial_.load(std::memory_order_acquire)){wire=next;return true;}
        }
        wire={};return false;
    }
private:
    std::atomic<std::uint64_t> serial_{};
    std::array<std::atomic<std::uint64_t>,sizeof(PowerHistoryWire)/8> words_{};
};

// Policy runs only on the existing PSC thread. It never performs radio/FS work.
// Adapter operations preserve one-shot attempts; waking never launches or arms.
class PowerLifecycle {
public:
    PowerLifecycle(){history_.Publish(wire_);}
    template<class Adapter>
    void Request(std::uint32_t state,std::uint32_t flags,std::uint32_t result,
                 std::uint64_t now,Adapter &adapter) {
        pending_=true;resume_=false;
        const auto previous=wire_.last_state;
        if(wire_.count==wire_.events.size()) {
            for(unsigned i=1;i<wire_.count;++i)wire_.events[i-1]=wire_.events[i];
        } else ++wire_.count;
        auto &event=wire_.events[wire_.count-1];
        event={now,state,previous,flags,result,UINT32_MAX,wire_.gate};
        if(wire_.total!=UINT32_MAX)++wire_.total;
        ++wire_.sequence;
        if(result || state>unsigned(PowerState::ShutdownReady)) {
            Terminal(adapter,CaptureCloseReason::PowerError);
        } else {
            wire_.last_state=state;
            if(state==unsigned(PowerState::ShutdownReady))Terminal(adapter,CaptureCloseReason::ShutdownRequest);
            else if(wire_.gate!=unsigned(PowerGate::Terminal)) {
                if(state==unsigned(PowerState::SleepReady) || state==unsigned(PowerState::EssentialSleepReady) ||
                   state==unsigned(PowerState::EssentialAwake)) {
                    if(adapter.PauseUnused())wire_.gate=unsigned(PowerGate::PausedUnused);
                    else {
                        if constexpr(requires{adapter.PauseSession();}) {
                            if(adapter.PauseSession())wire_.gate=unsigned(PowerGate::PausedSession);
                            else Terminal(adapter,CaptureCloseReason::SleepRequest);
                        } else Terminal(adapter,CaptureCloseReason::SleepRequest);
                    }
                } else if(state==unsigned(PowerState::FullAwake) ||
                          (previous==unsigned(PowerState::EssentialAwake) && state==unsigned(PowerState::MinimumAwake))) {
                    // FullAwake is explicit. The other wake edge matches lm.
                    // Do not resume before a successful PSC acknowledgement.
                    resume_=wire_.gate==unsigned(PowerGate::PausedUnused);
                    if constexpr(requires{adapter.ResumeSession();})
                        resume_=resume_ || (state==unsigned(PowerState::FullAwake) && wire_.gate==unsigned(PowerGate::PausedSession));
                }
            }
        }
        event.gate=wire_.gate;history_.Publish(wire_);
    }
    template<class Adapter>
    void Acknowledge(std::uint32_t result,Adapter &adapter) {
        if(!pending_)return;
        pending_=false;
        if(result)Terminal(adapter,CaptureCloseReason::PowerError);
        else if(resume_) {
            bool resumed{};
            if constexpr(requires{adapter.ResumeSession();}) {
                if(wire_.gate==unsigned(PowerGate::PausedSession))resumed=adapter.ResumeSession();
                else resumed=adapter.ResumeUnused();
            } else resumed=adapter.ResumeUnused();
            if(resumed)wire_.gate=unsigned(PowerGate::Open);else Terminal(adapter,CaptureCloseReason::SleepRequest);
        }
        wire_.events[wire_.count-1].ack_result=result;
        wire_.events[wire_.count-1].gate=wire_.gate;
        ++wire_.sequence;history_.Publish(wire_);
    }
    bool Read(PowerHistoryWire &wire,std::uint64_t now) const {
        if(!history_.Read(wire))return false;
        wire.now_boot_ms=now;return true;
    }
private:
    template<class Adapter> void Terminal(Adapter &adapter,CaptureCloseReason reason) {
        wire_.gate=unsigned(PowerGate::Terminal);adapter.Close(reason);
    }
    PowerHistoryWire wire_{};
    PowerHistory history_;
    bool pending_{},resume_{};
};
inline bool ValidPowerHistory(const PowerHistoryWire &w,std::uint64_t now) {
    if(w.magic!=0x41525057 || w.version!=1 || w.count>16 || w.total<w.count ||
       w.last_state>6 || w.gate>3 || w.reserved || !w.now_boot_ms || w.now_boot_ms>now ||
       now-w.now_boot_ms>1500)return false;
    for(unsigned i=0;i<w.count;++i)
        if(!w.events[i].boot_ms || w.events[i].boot_ms>w.now_boot_ms || w.events[i].previous>6 ||
           w.events[i].gate>3 || (i && w.events[i].boot_ms<w.events[i-1].boot_ms))return false;
    return true;
}
}
