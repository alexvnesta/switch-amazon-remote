// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "protocol.hpp"
namespace armodule {
// First automatic-startup qualification. The unchanged C2 companion remains
// the one-attempt-per-boot authority. Never reset its native callback lifetime.
class AutomaticStart {
public:
    enum class Action { None, Arm, Link, Stop };
    Action Next(std::uint64_t now, bool full_awake, bool healthy_idle,
                State state, bool peer_ready) {
        if (!now || (last_ && now < last_)) { terminal_ = true; return Action::Stop; }
        last_ = now;
        if (terminal_) return Action::None;
        if (state != State::Idle && state != State::Scanning && state != State::Linking && state != State::Ready) {
            terminal_ = true; return Action::None;
        }
        if (!full_awake) return Action::None;
        if (!awake_since_) awake_since_ = now;
        if (!armed_ && state == State::Idle) {
            if (now - awake_since_ >= 120000) { terminal_ = true; return Action::Stop; }
            if (healthy_idle) { armed_ = true; return Action::Arm; }
        }
        if (armed_ && !linked_ && state == State::Scanning && peer_ready) {
            linked_ = true; return Action::Link;
        }
        return Action::None;
    }
    bool Consumed() const { return armed_ || terminal_; }
    // Startup may pass through sleep states before HOME. Only an UNUSED
    // instance can wait for the next fully-awake transition.
    void PauseUnused() { if (!Consumed()) awake_since_ = 0; }
private:
    std::uint64_t last_{}, awake_since_{};
    bool armed_{}, linked_{}, terminal_{};
};
}
