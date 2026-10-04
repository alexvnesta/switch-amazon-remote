// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "memory_check.hpp"
namespace armodule {
inline constexpr char LayoutOpenPath[]="sdmc:/config/amazon-remote-module/layout-diag5-open.bin";
inline constexpr char LayoutLaterPath[]="sdmc:/config/amazon-remote-module/layout-diag5-5s.bin";
enum RegionGuard : std::uint32_t {
    GuardCodeType=1, GuardRx=2, GuardExtent=4, GuardAttributes=8, GuardCandidate=16
};
struct RegionRecord {
    std::uint64_t address{},size{};
    std::uint32_t type{},attributes{},permissions{},guards{};
};
static_assert(sizeof(RegionRecord)==32);
struct LayoutRecord {
    std::uint32_t magic{0x41524c35},version{1},count{},complete{};
    MemoryRecord report{};
    std::array<RegionRecord,128> entries{};
    std::uint32_t reserved{},checksum{};
};
static_assert(sizeof(LayoutRecord)==4312 && std::is_trivially_copyable_v<LayoutRecord>);
inline std::uint32_t LayoutChecksum(const LayoutRecord &r) {
    const auto *p=reinterpret_cast<const unsigned char *>(&r);std::uint32_t h=2166136261u;
    for (std::size_t i=0;i<offsetof(LayoutRecord,checksum);++i) h=(h^p[i])*16777619u;
    return h;
}
inline bool ValidLayoutRecord(const LayoutRecord &r) {
    return r.magic==0x41524c35 && r.version==1 && r.count<=128 && r.complete<=1 && !r.reserved &&
        ValidMemoryRecord(r.report) && r.checksum==LayoutChecksum(r);
}
inline bool SaveLayoutExclusive(const char *path,const LayoutRecord &r) {
    if (!ValidLayoutRecord(r)) return false;
    FILE *f=std::fopen(path,"wbx");if (!f) return false;
    const bool wrote=std::fwrite(&r,1,sizeof r,f)==sizeof r;
    const bool flushed=std::fflush(f)==0,synced=::fsync(::fileno(f))==0,closed=std::fclose(f)==0;
    LayoutRecord copy{};
    return wrote && flushed && synced && closed && ReadExact(path,copy) && !std::memcmp(&r,&copy,sizeof r);
}
}
