// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
namespace armodule {
// Only redundant data dumps are omitted. Keep API results, state transitions,
// GATT event headers, identity/security decisions, output ACKs and all errors.
inline bool RedundantLog(std::string_view s) {
    return s.starts_with("raw notification ") || s.starts_with("input report=") ||
        s.starts_with("buttons pressed=") || s.starts_with("HID_METADATA_RAW ") ||
        s.starts_with("HID_METADATA before_security index=") ||
        s.starts_with("HID_METADATA after_security index=");
}
struct LogTiming {
    std::uint64_t writes{},omitted{},total_ms{},max_ms{},invalid_clocks{};
    static void Increment(std::uint64_t &n) { if(n!=std::numeric_limits<std::uint64_t>::max())++n; }
    void Omit() { Increment(omitted); }
    void Record(std::uint64_t before,std::uint64_t after) {
        Increment(writes);
        if(after<before){Increment(invalid_clocks);return;}
        const auto duration=after-before;
        max_ms=std::max(max_ms,duration);
        total_ms+=std::min(duration,std::numeric_limits<std::uint64_t>::max()-total_ms);
    }
};
}
