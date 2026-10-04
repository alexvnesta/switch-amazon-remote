// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "startup.hpp"
#include <array>

namespace armodule {
// Experimental, inactive candidate. Deliberately outside NCM ApplicationId range.
inline constexpr std::uint64_t SafeModuleProgramId = 0x010000000000a4d0;
inline constexpr std::uint32_t FixNotAttempted = 0xffffffffu, FixProcessNotFound = 0x20fu;
inline constexpr char OverridePath[] = "sdmc:/config/amazon-remote-module/override-diag6.bin";
inline constexpr char FixPath[] = "sdmc:/config/amazon-remote-module/launch-fix6.bin";
inline constexpr char FixTempPath[] = "sdmc:/config/amazon-remote-module/launch-fix6.tmp";
inline constexpr char SafeModulePath[] = "sdmc:/atmosphere/contents/010000000000a4d0/exefs.nsp";
inline constexpr std::size_t SafeModuleSize = 142150;
inline constexpr std::array<std::uint8_t,32> SafeModuleHash{
    0xea,0x57,0xa5,0x10,0x73,0x9b,0x9c,0x28,0xbf,0x02,0xff,0xe2,0xed,0x60,0x6d,0x57,
    0x11,0x8e,0x3d,0xa4,0x8a,0x40,0x2d,0x2a,0xf3,0x44,0x2a,0xe9,0x8e,0x11,0x94,0xc1};
struct OverrideRecord {
    std::uint32_t magic{0x41524f36}, version{1}, init_result{FixNotAttempted}, query_result{FixNotAttempted};
    std::uint64_t now_ms{}, pid{}, location_program_id{}, keys_held{}, flags{};
    std::uint32_t info_result{FixNotAttempted}, close_result{FixNotAttempted}, identity_matches{}, hbl{};
    std::uint32_t reserved{}, checksum{};
};
static_assert(sizeof(OverrideRecord)==80 && offsetof(OverrideRecord,checksum)==76);
template<class T> std::uint32_t FixChecksum(const T &record) {
    const auto *p=reinterpret_cast<const unsigned char *>(&record);
    std::uint32_t hash=2166136261u;
    for (std::size_t i=0;i<offsetof(T,checksum);++i) hash=(hash^p[i])*16777619u;
    return hash;
}
inline bool ValidOverride(const OverrideRecord &r) {
    return r.magic==0x41524f36 && r.version==1 && r.now_ms && !r.reserved &&
        r.identity_matches<=1 && r.hbl<=1 && r.checksum==FixChecksum(r);
}
inline bool ConfirmedLegacyHbl(const OverrideRecord &r) {
    return ValidOverride(r) && !r.init_result && !r.query_result && !r.info_result &&
        !r.close_result && r.pid && r.identity_matches && r.location_program_id==ModuleProgramId &&
        r.hbl && (r.flags&1);
}
inline bool SafeAbsent(const OverrideRecord &r) {
    return ValidOverride(r) && !r.init_result && r.query_result==FixProcessNotFound && !r.pid &&
        r.info_result==FixNotAttempted && !r.identity_matches;
}
struct FixRecord {
    std::uint32_t magic{0x41524636}, version{1}, stage{}, reserved{};
    OverrideRecord legacy{}, before{}, after{};
    std::uint64_t issued_ms{}, returned_ms{}, returned_pid{};
    std::uint32_t hash_verified{}, shell_result{FixNotAttempted}, launch_result{FixNotAttempted}, buttons_released{};
    std::array<std::uint8_t,32> module_sha256{};
    std::uint32_t reserved2{}, checksum{};
};
static_assert(sizeof(FixRecord)==336 && offsetof(FixRecord,checksum)==332);
inline bool ValidFix(const FixRecord &r) {
    return r.magic==0x41524636 && r.version==1 && r.stage>=1 && r.stage<=4 &&
        !r.reserved && !r.reserved2 && r.issued_ms && ConfirmedLegacyHbl(r.legacy) && SafeAbsent(r.before) &&
        r.hash_verified==1 && r.buttons_released==1 && r.module_sha256==SafeModuleHash &&
        (r.stage!=4 || ValidOverride(r.after)) && r.checksum==FixChecksum(r);
}
// The UI cannot request launch while A/R/any other button is held. It arms on
// one fresh A-only edge and consumes the attempt after a full neutral second.
class ReleasedLaunchGate {
    bool ready_{}, armed_{}, consumed_{};
    std::uint64_t neutral_since_{}, previous_ms_{};
public:
    bool Poll(std::uint64_t now, bool a_only_edge, bool all_released) {
        if (consumed_) return false;
        if (now<previous_ms_) { ready_=false; armed_=false; neutral_since_=0; }
        previous_ms_=now;
        if (!armed_ && all_released) ready_=true;
        if (ready_ && a_only_edge) { armed_=true; neutral_since_=0; }
        if (!armed_) return false;
        if (!all_released) { neutral_since_=0; return false; }
        if (!neutral_since_) { neutral_since_=now; return false; }
        if (now-neutral_since_<1000) return false;
        consumed_=true; return true;
    }
    bool Consumed() const { return consumed_; }
};
enum class FixOutcome { LegacyNotHbl, SafeNotAbsent, HashMismatch, IntentFailed, ShellFailed, Returned, ReceiptFailed };
template<class S> FixOutcome RequestFix(S &sdk, FixRecord &r) {
    r={}; r.legacy=sdk.Capture(false);
    if (!ConfirmedLegacyHbl(r.legacy)) return FixOutcome::LegacyNotHbl;
    r.before=sdk.Capture(true);
    if (!SafeAbsent(r.before)) return FixOutcome::SafeNotAbsent;
    if (!sdk.Verify(r.module_sha256)) return FixOutcome::HashMismatch;
    r.issued_ms=sdk.Now(); r.hash_verified=1; r.buttons_released=1; r.stage=1;
    r.checksum=FixChecksum(r);
    if (!sdk.CreateIntent(r)) return FixOutcome::IntentFailed;
    r.shell_result=sdk.InitializeShell();
    if (r.shell_result) { r.stage=2; r.returned_ms=sdk.Now(); }
    else {
        r.launch_result=sdk.Launch(r.returned_pid); r.returned_ms=sdk.Now(); r.stage=3;
        sdk.CloseShell();
    }
    r.checksum=FixChecksum(r);
    if (!sdk.Update(r)) return FixOutcome::ReceiptFailed;
    return r.shell_result ? FixOutcome::ShellFailed : FixOutcome::Returned;
}
}
