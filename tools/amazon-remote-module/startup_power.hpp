// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace armodule {
// Construct only after successful PSC registration. Like MissionControl's main
// loop, a newly running module is initially operational, not waiting for a
// transition which PSC may never deliver at cold boot. This is NOT a queried
// system FullAwake state. Health qualification and transport preflights remain
// mandatory. Once any power event is seen, the bootstrap grant is gone forever.
class StartupPower {
public:
    explicit StartupPower(bool registered) : phase_(registered ? Phase::Startup : Phase::Terminal) {}
    void EventObserved() {
        if (pending_ || phase_ == Phase::Terminal) { Fail(); return; }
        phase_ = Phase::Waiting; pending_ = true; request_ok_ = false;
    }
    void Request(std::uint32_t state, std::uint32_t result) {
        if (!pending_ || request_ok_ || result || state >= 5 || phase_ == Phase::Terminal) { Fail(); return; }
        state_ = state; request_ok_ = true;
    }
    void Acknowledge(std::uint32_t result) {
        if (!pending_ || !request_ok_ || result || phase_ == Phase::Terminal) { Fail(); return; }
        pending_ = false; request_ok_ = false;
        phase_ = state_ == 0 ? Phase::FullAwake : Phase::Waiting;
    }
    bool CanStart() const { return phase_ == Phase::Startup || phase_ == Phase::FullAwake; }
    bool CanResume() const { return phase_ == Phase::FullAwake; }
    bool IsBootstrap() const { return phase_ == Phase::Startup; }
    bool Failed() const { return phase_ == Phase::Terminal; }
private:
    enum class Phase { Startup, Waiting, FullAwake, Terminal };
    void Fail() { phase_ = Phase::Terminal; pending_ = request_ok_ = false; }
    Phase phase_;
    std::uint32_t state_{6};
    bool pending_{}, request_ok_{};
};
}
