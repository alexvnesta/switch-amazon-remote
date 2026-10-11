// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "startup.hpp"
#include "protocol.hpp"
namespace armodule {
// Old checksummed status is not current just because its boot-time value
// happens to overlap this boot. Bind it to the independently written startup.
inline bool CurrentStatus(const Status &status,const StartupRecord &startup,std::uint64_t now) {
    return ValidStartup(startup) && !startup.result && startup.now_ms && startup.now_ms<=now &&
        startup.instance==status.instance && startup.stage==unsigned(StartupStage::Status) &&
        FreshStatus(status,now);
}
}
