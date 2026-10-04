// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hdls_trace.hpp"
#include <array>
namespace armodule {
inline constexpr std::uint64_t NativeHdlsProgramId=0x010000000000a4d2;
inline constexpr char NativePermitPath[]="sdmc:/config/amazon-remote-module/hdls-diag8-permit.bin";
inline constexpr char NativeUsedPath[]="sdmc:/config/amazon-remote-module/hdls-diag8-used.bin";
inline constexpr char NativeAttemptPath[]="sdmc:/config/amazon-remote-module/hdls-diag8-launch.bin";
inline constexpr char NativeAttemptTemp[]="sdmc:/config/amazon-remote-module/hdls-diag8-launch.tmp";
inline constexpr char NativeResultPath[]="sdmc:/config/amazon-remote-module/hdls-diag8-result.bin";
inline constexpr char NativeResultTemp[]="sdmc:/config/amazon-remote-module/hdls-diag8-result.tmp";
template<class T> std::uint32_t NativeChecksum(const T &r) {
    const auto *p=reinterpret_cast<const unsigned char *>(&r); std::uint32_t hash=2166136261u;
    for (std::size_t i=0;i<offsetof(T,checksum);++i) hash=(hash^p[i])*16777619u;
    return hash;
}
struct NativePermit {
    std::uint32_t magic{0x41525038},version{1},neutral_only{1},buttons_released{1};
    std::uint64_t program_id{NativeHdlsProgramId},issued_ms{};
    std::uint32_t reserved{},checksum{};
};
static_assert(sizeof(NativePermit)==40);
inline bool ValidNativePermit(const NativePermit &r,std::uint64_t now) {
    return r.magic==0x41525038 && r.version==1 && r.neutral_only==1 && r.buttons_released==1 &&
        r.program_id==NativeHdlsProgramId && r.issued_ms && now>=r.issued_ms && now-r.issued_ms<=2000 &&
        !r.reserved && r.checksum==NativeChecksum(r);
}
struct NativeResult {
    std::uint32_t magic{0x41525238},version{1},stage{},result{};
    std::uint64_t instance{},now_ms{},permit_issued_ms{};
    // diag8: heap_result describes static allocator-arena setup, not a kernel
    // heap request. QueryMemory in trace supplies the actual backing type.
    std::uint32_t heap_result{HdlsNotAttempted},sm_result{HdlsNotAttempted},fs_result{HdlsNotAttempted};
    std::uint32_t mount_result{HdlsNotAttempted},firmware_result{HdlsNotAttempted},permit_consumed{};
    HdlsTrace trace{};
    std::uint32_t reserved{},checksum{};
};
static_assert(sizeof(NativeResult)==192);
inline bool ValidNativeResult(const NativeResult &r) {
    return r.magic==0x41525238 && r.version==1 && r.stage>=1 && r.stage<=3 && r.instance && r.now_ms &&
        r.permit_issued_ms && r.permit_consumed==1 && !r.reserved && r.checksum==NativeChecksum(r) &&
        (r.stage!=3 || (ValidHdlsTrace(r.trace) && r.trace.instance==r.instance && r.trace.now_ms<=r.now_ms));
}
struct NativeAttempt {
    std::uint32_t magic{0x41524c38},version{1},stage{},reserved{};
    std::uint64_t issued_ms{},returned_ms{},returned_pid{};
    std::uint32_t query_init{HdlsNotAttempted},query_result{HdlsNotAttempted},hash_verified{};
    std::uint32_t shell_result{HdlsNotAttempted},launch_result{HdlsNotAttempted},buttons_released{};
    std::array<std::uint8_t,32> module_sha256{};
    std::uint32_t reserved2{},checksum{};
};
static_assert(sizeof(NativeAttempt)==104);
inline bool ValidNativeAttempt(const NativeAttempt &r) {
    return r.magic==0x41524c38 && r.version==1 && r.stage>=1 && r.stage<=3 && !r.reserved && !r.reserved2 &&
        r.issued_ms && r.query_init==0 && r.query_result==0x20f && r.hash_verified==1 && r.buttons_released==1 &&
        r.checksum==NativeChecksum(r);
}
}
