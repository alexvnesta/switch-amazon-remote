// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace amazon_remote {
// Opt-in sustained test: setup retains its original connected 90s budget.
// Extension starts only after validated subscriptions reach Ready in time.
class SessionBudget {
public:
    static constexpr std::uint64_t SetupMs=90000, SustainedMs=1800000;
    explicit SessionBudget(bool sustained=false):sustained_(sustained) {}
    bool Expired(std::uint64_t linked,std::uint64_t now) const {
        if (now<linked) return true;
        if (sustained_ && ready_) return now<ready_ms_ || now-ready_ms_>=SustainedMs;
        return now-linked>=SetupMs;
    }
    bool AdmitReady(std::uint64_t linked,std::uint64_t now) {
        if (Expired(linked,now)) return false;
        if (sustained_ && !ready_) { ready_ms_=now;ready_=true; }
        return true;
    }
    bool Sustained() const { return sustained_; }
    void Reset() { ready_ms_=0;ready_=false; }
private:
    const bool sustained_;
    bool ready_{};
    std::uint64_t ready_ms_{};
};
}
