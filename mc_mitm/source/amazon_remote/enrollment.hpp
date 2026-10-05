// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace amazon_remote {
enum class EnrollmentState:std::uint32_t {None,Refused,Prepared,Accepted,Listed,AlreadyListed,Failed,TimedOut,Cancelled};
struct EnrollmentRecord {
    std::uint32_t magic{0x41524531},version{1},state{},result{};
    std::uint32_t connection{},request_issued{},before_result{0xffffffff},before_count{},before_matches{},
        after_result{0xffffffff},after_count{},after_matches{};
    std::uint64_t instance{},requested_ms{},deadline_ms{},last_query_ms{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(EnrollmentRecord)==88 && offsetof(EnrollmentRecord,checksum)==80);
inline std::uint32_t EnrollmentChecksum(const EnrollmentRecord &r) {
    const auto *p=reinterpret_cast<const unsigned char *>(&r);std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<offsetof(EnrollmentRecord,checksum);++i)hash=(hash^p[i])*16777619u;
    return hash;
}
inline bool ValidEnrollment(const EnrollmentRecord &r) {
    return r.magic==0x41524531 && r.version==1 && !r.reserved && r.instance &&
        r.state>unsigned(EnrollmentState::None) && r.state<=unsigned(EnrollmentState::Cancelled) &&
        r.request_issued<=1 && r.checksum==EnrollmentChecksum(r);
}
using EnrollmentIntent=bool(*)(const EnrollmentRecord &);
}
