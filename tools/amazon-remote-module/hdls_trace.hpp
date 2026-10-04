// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>
namespace armodule {
inline constexpr std::uint32_t HdlsNotAttempted=0xffffffffu;
struct HdlsTrace {
    std::uint32_t magic{0x41524837},version{1};
    std::uint64_t instance{},now_ms{},buffer_address{},region_address{},region_size{};
    std::uint32_t memory_type{},permission{},attributes{},allocation_ok{};
    std::uint32_t open_result{HdlsNotAttempted},memory_query_result{HdlsNotAttempted};
    std::uint32_t attach_session_result{HdlsNotAttempted},attach_device_result{HdlsNotAttempted};
    std::uint32_t first_set_result{HdlsNotAttempted},last_set_result{HdlsNotAttempted};
    std::uint32_t detach_result{HdlsNotAttempted},release_result{HdlsNotAttempted};
    std::uint32_t set_calls{},closed{},freed{},quarantined{},reserved{},checksum{};
};
static_assert(sizeof(HdlsTrace)==120 && offsetof(HdlsTrace,checksum)==116);
inline std::uint32_t HdlsTraceChecksum(const HdlsTrace &r) {
    const auto *p=reinterpret_cast<const unsigned char *>(&r); std::uint32_t hash=2166136261u;
    for (std::size_t i=0;i<offsetof(HdlsTrace,checksum);++i) hash=(hash^p[i])*16777619u;
    return hash;
}
inline bool ValidHdlsTrace(const HdlsTrace &r) {
    return r.magic==0x41524837 && r.version==1 && r.instance && r.now_ms && !r.reserved &&
        r.allocation_ok<=1 && r.closed<=1 && r.freed<=1 && r.quarantined<=1 && !(r.freed && r.quarantined) &&
        r.checksum==HdlsTraceChecksum(r);
}
}
