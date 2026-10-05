// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "protocol.hpp"
namespace armodule {
// Distinct fresh, successful IDLE snapshots from one instance for15seconds.
// Shared by publisher and UI; loss, errors or clock/instance changes restart it.
class IdleHealth {
    std::uint64_t instance_{},first_{},last_{};unsigned samples_{};
public:
    void Reset() { instance_=first_=last_=0;samples_=0; }
    void Observe(const Status &s,std::uint64_t now) {
        if (!FreshStatus(s,now) || s.state!=unsigned(State::Idle) || s.result || s.output_attached || s.link_ready || s.buttons) { Reset();return; }
        if (instance_!=s.instance || (last_ && (s.now_ms<last_ || s.now_ms-last_>2000))) Reset();
        if (!first_) { instance_=s.instance;first_=s.now_ms; }
        if (s.now_ms!=last_) { last_=s.now_ms;if(samples_<1000)++samples_; }
    }
    bool Ready(std::uint64_t now) const { return first_ && last_>=first_ && now>=last_ && now-last_<=2000 && last_-first_>=15000 && samples_>=20; }
    unsigned Seconds() const { return last_>=first_ && first_?unsigned((last_-first_)/1000):0; }
};
// UI still handles buttons at50Hz, but never opens status more than4Hz. Cached
// status is revalidated for freshness on EVERY permission decision.
class StatusReadSchedule {
    std::uint64_t last_{};
public:
    bool Due(std::uint64_t now) {
        if (!now) return false;
        if (!last_ || now<last_ || now-last_>=250) { last_=now;return true; }
        return false;
    }
};
// Dephase transient read/write sharing locks instead of retrying every500ms
// in sync with the reader. No retry of failed writes or non-sharing FS errors.
class StatusWriteSchedule {
    std::uint64_t next_{},failure_{};unsigned retries_{};
public:
    bool Due(std::uint64_t now) const { return !next_ || now>=next_; }
    bool Complete(Result rc,bool permanent,std::uint64_t now) {
        if (!rc) { next_=now+500;failure_=0;retries_=0;return false; }
        if (!failure_) failure_=now;
        if (permanent || rc!=0xe02 || !now || now<failure_ || now-failure_>=2000) return true;
        next_=now+37+(retries_++%5)*11;return false;
    }
    bool Retrying() const { return failure_!=0; }
};
}
