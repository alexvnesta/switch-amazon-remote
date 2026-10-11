// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "native_bond_trial.hpp"
#include "native_bond_guard.hpp"
namespace arnativebond {
enum class GuardStage:std::uint32_t {
    NotReached, Binary, ProcessInit, ProcessQuery, Storage, StartupRead,
    StartupInvalid, ContextRead, StatusRead, ConnectedReady, ConnectionOpen,
    ConnectionGetter, ListBounds, ListInvalid, TargetMissing, ContextBinding, Passed
};
enum DiagnosticFlags:std::uint32_t {
    BinaryMatched=1, ProcessPresent=2, StartupRead=4, ContextRead=8,
    StatusRead=16, AruidObserved=32, ConnectionsRead=64
};
// Address/key-free metadata. Flags mean OBSERVED, not passed. Failed nested
// records remain useful evidence and do not grant any command permission.
struct GuardDiagnostic {
    // Version2 records the system btm getter; version1 recorded app btmu.
    // Exact layout retained, version distinguishes field semantics.
    std::uint32_t magic{0x41524744},version{2},call{},stage{};
    std::uint32_t result{Unknown},flags{},firmware{},aruid_zero{Unknown};
    std::uint64_t begin_ms{},end_ms{},check_ms{};
    std::uint32_t pm_init{Unknown},pm_query{Unknown},status_read{Unknown},connection_open{Unknown},connection_getter{Unknown};
    std::uint32_t count{Unknown},matches{Unknown},invalid{Unknown},live_handle{},query_result{Unknown};
    armodule::StartupRecord startup{};
    armodule::Status status{};
    amazon_remote::EnrollmentContextRecord context{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(GuardDiagnostic)==400 && offsetof(GuardDiagnostic,checksum)==392);
inline std::uint32_t DiagnosticChecksum(const GuardDiagnostic &r){
    const auto *p=reinterpret_cast<const unsigned char *>(&r);std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<offsetof(GuardDiagnostic,checksum);++i)h=(h^p[i])*16777619u;
    return h;
}
inline bool ValidDiagnostic(const GuardDiagnostic &r){
    return r.magic==0x41524744 && r.version==2 && r.call>=1 && r.call<=2 &&
        r.stage>=unsigned(GuardStage::Binary) && r.stage<=unsigned(GuardStage::Passed) &&
        !(r.flags&~127u) && !r.reserved && r.checksum==DiagnosticChecksum(r) &&
        r.begin_ms && r.end_ms>=r.begin_ms &&
        r.check_ms>=r.begin_ms && r.check_ms<=r.end_ms &&
        ((r.flags&AruidObserved)?r.aruid_zero<=1:r.aruid_zero==Unknown) &&
        (r.stage==unsigned(GuardStage::Passed)?r.result==0:r.result!=0);
}
inline const char *GuardReason(GuardStage stage){
    constexpr const char *names[]={
        "Guard was not reached.","Setup module file does not match this trial.",
        "Process service could not be opened.","Setup process could not be verified.",
        "SD filesystem is not available.","Setup checkpoint could not be read.",
        "Setup checkpoint is invalid.","Connection check NOT SAVED. Use Y in Setup first.",
        "Setup status is not fresh or cannot be read.",
        "Setup is not connected, or a remote button is held.",
        "System connection getter could not be opened.","System connection getter returned an error.",
        "Current connection list may be truncated.","Current connection list is invalid.",
        "System list does not contain exactly one target connection.",
        "Saved check expired or no longer matches the connection.","Connection guard passed."
    };
    const auto index=unsigned(stage);return index<=unsigned(GuardStage::Passed)?names[index]:"Unknown guard stage.";
}
}
