// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ardriverid {
inline constexpr std::uint64_t ProgramId = 0x010000000000000b;
inline constexpr std::uint32_t Firmware = 0x160500, Unknown = 0xffffffff;
inline constexpr std::size_t Capacity = 16;
using BuildId = std::array<std::uint8_t, 32>;
using Inventory = std::array<BuildId, Capacity>;
// NSO header metadata of the independently verified stored 22.5.0 image.
// Matching loader metadata does NOT prove unmodified runtime bytes or backend.
inline constexpr BuildId Expected{0xc9,0x1c,0x6f,0xc8,0xaa,0x4c,0x39,0x22,
    0x2d,0x6c,0xcf,0xe0,0xff,0xf4,0x68,0x54,0x31,0x05,0xe5,0x9b};
enum class Verdict : std::uint32_t { UnsupportedFirmware=1, QueryFailed, ProcessChanged,
    NoModules, InvalidInventory, KnownBuildMetadata, OtherBuildMetadata };
struct Record {
    std::uint32_t magic{0x41524944}, version{1}, firmware{}, verdict{};
    std::uint64_t program_id{ProgramId}, begin_ms{}, end_ms{}, pid_before{}, pid_after{};
    std::uint32_t pm_init{Unknown}, pm_before{Unknown}, loader_init{Unknown},
                  modules_result{Unknown}, pm_after{Unknown};
    std::int32_t count{-1};
    std::uint32_t matches{}, checksum{};
    Inventory build_ids{}; // No load addresses, process memory or key material.
};
static_assert(sizeof(Record)==600 && offsetof(Record,checksum)==84 && offsetof(Record,build_ids)==88);
static_assert(std::is_trivially_copyable_v<Record> && std::is_standard_layout_v<Record>);
inline Verdict Evaluate(const Record &r) {
    if (r.firmware!=Firmware) return Verdict::UnsupportedFirmware;
    if (r.pm_init || r.pm_before || r.loader_init || r.modules_result || r.pm_after || !r.pid_before)
        return Verdict::QueryFailed;
    if (r.pid_before!=r.pid_after) return Verdict::ProcessChanged;
    if (!r.count) return Verdict::NoModules;
    if (r.count<0 || std::size_t(r.count)>=Capacity) return Verdict::InvalidInventory;
    unsigned matches{};
    for (unsigned i=0; i<unsigned(r.count); ++i) {
        if (r.build_ids[i]==BuildId{}) return Verdict::InvalidInventory;
        for (unsigned j=0; j<i; ++j)
            if (r.build_ids[i]==r.build_ids[j]) return Verdict::InvalidInventory;
        matches += r.build_ids[i]==Expected;
    }
    return matches==1 ? Verdict::KnownBuildMetadata : Verdict::OtherBuildMetadata;
}
inline std::uint32_t Checksum(const Record &r) {
    const auto *p=reinterpret_cast<const unsigned char *>(&r);
    std::uint32_t h=2166136261;
    for (std::size_t i=0; i<sizeof r; ++i)
        if (i<offsetof(Record,checksum) || i>=offsetof(Record,checksum)+4) h=(h^p[i])*16777619;
    return h;
}
inline bool Valid(const Record &r) {
    if (r.magic!=0x41524944 || r.version!=1 || r.program_id!=ProgramId || !r.begin_ms ||
        r.end_ms<r.begin_ms || r.end_ms-r.begin_ms>10000 || r.checksum!=Checksum(r) ||
        r.verdict!=unsigned(Evaluate(r))) return false;
    unsigned matches{};
    for (const auto &id:r.build_ids) matches += id==Expected;
    return r.matches==matches;
}
// Only process/loader getters. No debug attachment, target memory access,
// driver service, radio API, pairing API or process launch is available here.
template<class Backend> Record Collect(Backend &b) {
    Record r{}; r.firmware=b.FirmwareVersion(); r.begin_ms=b.Now();
    if (r.firmware==Firmware) {
        r.pm_init=b.PmOpen();
        if (!r.pm_init) {
            r.pm_before=b.ProcessId(ProgramId,r.pid_before);
            if (!r.pm_before && r.pid_before) {
                r.loader_init=b.LoaderOpen();
                if (!r.loader_init) {
                    r.modules_result=b.Modules(r.pid_before,r.build_ids,r.count);
                    b.LoaderClose();
                    r.pm_after=b.ProcessId(ProgramId,r.pid_after);
                }
            }
            b.PmClose();
        }
    }
    for (const auto &id:r.build_ids) r.matches += id==Expected;
    r.end_ms=b.Now(); r.verdict=unsigned(Evaluate(r)); r.checksum=Checksum(r);
    return r;
}
}
