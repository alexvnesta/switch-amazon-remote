// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace amazon_remote {
// Suppresses identical successful health messages, never service polling.
// Callers must bypass this gate on errors or missing owned resources.
class HealthLogGate {
public:
    bool Due(std::uint64_t now,std::uint32_t state) {
        if (!seen_ || state!=state_ || now<last_ || now-last_>=30000) {
            seen_=true;last_=now;state_=state;return true;
        }
        return false;
    }
    void Reset() { seen_=false;last_=0;state_=0; }
private:
    bool seen_{};
    std::uint64_t last_{};
    std::uint32_t state_{};
};
}
