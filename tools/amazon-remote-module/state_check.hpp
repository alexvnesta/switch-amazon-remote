// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "startup.hpp"
#include "io.hpp"

namespace armodule {
inline constexpr std::uint32_t NotAttempted = 0xffffffffu;
inline constexpr char StateOpenPath[] = "sdmc:/config/amazon-remote-module/state-diag3-open.bin";
inline constexpr char StateLaterPath[] = "sdmc:/config/amazon-remote-module/state-diag3-5s.bin";
struct StateRecord {
    std::uint32_t magic{0x41525333}, version{1}, phase{}, reserved{};
    std::uint64_t now_ms{}, pid{}, location_program_id{};
    std::uint32_t init_result{NotAttempted}, query_result{NotAttempted};
    std::uint32_t jit_result{NotAttempted}, jit_count{}, jit_found{}, jit_at_capacity{};
    std::uint32_t info_result{NotAttempted}, identity_matches{}, syscall_hinted{}, state_attempted{};
    std::uint32_t state_result{NotAttempted}, state_valid{}, close_attempted{}, close_result{NotAttempted};
    std::int64_t state{-1};
    std::uint32_t reserved2{}, checksum{};
};
static_assert(sizeof(StateRecord) == 112 && std::is_trivially_copyable_v<StateRecord>);
inline std::uint32_t StateChecksum(const StateRecord &record) {
    const auto *p = reinterpret_cast<const unsigned char *>(&record);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i=0; i<offsetof(StateRecord, checksum); ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
inline bool ValidStateRecord(const StateRecord &record) {
    return record.magic == 0x41525333 && record.version == 1 && !record.reserved && !record.reserved2 &&
        (record.phase == 1 || record.phase == 2) && record.now_ms && record.jit_found <= 1 &&
        record.jit_at_capacity <= 1 && record.identity_matches <= 1 && record.syscall_hinted <= 1 &&
        record.state_attempted <= 1 && record.state_valid <= 1 && record.close_attempted <= 1 &&
        (!record.state_valid || (record.state_attempted && !record.state_result && record.identity_matches &&
            record.syscall_hinted && record.state >= 0 && record.state <= 7)) &&
        record.checksum == StateChecksum(record);
}
inline const char *ProcessStateName(const StateRecord &record) {
    constexpr const char *names[] = {"Created (not started)", "CreatedAttached", "Running", "Crashed",
        "RunningAttached", "Exiting", "Exited", "DebugSuspended"};
    return record.state_valid && record.state >= 0 && record.state <= 7 ? names[record.state] : "UNKNOWN (see Results)";
}
// Exclusive, bounded evidence: never replace a previous/partial diagnostic.
inline bool SaveStateExclusive(const char *path, const StateRecord &record) {
    if (!ValidStateRecord(record)) return false;
    FILE *file = std::fopen(path, "wbx"); if (!file) return false;
    const bool wrote = std::fwrite(&record, 1, sizeof record, file) == sizeof record;
    const bool flushed = std::fflush(file) == 0;
    const bool synced = ::fsync(::fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    StateRecord readback{};
    return wrote && flushed && synced && closed && ReadExact(path, readback) &&
        std::memcmp(&record, &readback, sizeof record) == 0;
}
}
