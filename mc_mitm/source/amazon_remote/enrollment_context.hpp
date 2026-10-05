// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../../tools/amazon-remote-pairing-audit/context.hpp"
namespace amazon_remote {
// Address-free snapshot bound to the actual navigation module instance.
// Query failures are evidence, not a pairing permission or a reason to retry.
struct EnrollmentContextRecord {
    std::uint32_t magic{0x41524331},version{1};
    std::uint64_t instance{};
    std::uint32_t expected_handle{},before_binding{},after_binding{},reserved0{};
    araudit::ContextSnapshot snapshot{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(EnrollmentContextRecord)==168 && offsetof(EnrollmentContextRecord,checksum)==160);
inline std::uint32_t EnrollmentContextChecksum(const EnrollmentContextRecord &r) {
    const auto *p=reinterpret_cast<const unsigned char *>(&r);std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<offsetof(EnrollmentContextRecord,checksum);++i)hash=(hash^p[i])*16777619u;
    return hash;
}
inline bool ValidEnrollmentContext(const EnrollmentContextRecord &r) {
    return r.magic==0x41524331 && r.version==1 && r.instance && r.expected_handle &&
        r.expected_handle!=0xffffffff && !r.reserved0 && !r.reserved &&
        r.snapshot.magic==0x41525033 && r.snapshot.version==3 && !r.snapshot.reserved0 && !r.snapshot.reserved &&
        r.snapshot.checksum==araudit::Checksum(r.snapshot) && r.checksum==EnrollmentContextChecksum(r);
}
}
