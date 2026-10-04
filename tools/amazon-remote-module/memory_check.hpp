// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "state_check.hpp"
#include <array>
namespace armodule {
inline constexpr char MemoryOpenPath[] = "sdmc:/config/amazon-remote-module/memory-diag4-open.bin";
inline constexpr char MemoryLaterPath[] = "sdmc:/config/amazon-remote-module/memory-diag4-5s.bin";
// Exact startup-diag1 ELF: pinned by SD image hash plus runtime code/build ID.
inline constexpr std::uint64_t TextExtent = 0x2b000, CodeOffset = 0x1af0;
inline constexpr std::uint64_t NoteOffset = 0x33590, RecordOffset = 0x35000;
inline constexpr std::array<unsigned char,64> CodeFingerprint{
    0xfd,0x7b,0xae,0xa9,0xfd,0x03,0x00,0x91,0xf3,0x0b,0x00,0xf9,0x21,0xe0,0x3b,0xd5,
    0xa0,0x01,0x00,0x90,0xb3,0x01,0x00,0xd0,0x73,0xc2,0x3b,0x91,0x01,0x08,0x00,0xf9,
    0x90,0x42,0x00,0x94,0xe1,0x03,0x00,0x2a,0x1f,0x00,0x00,0x71,0x60,0xba,0x01,0xb9,
    0xe0,0x17,0x9f,0x1a,0x60,0xf2,0x06,0x39,0x20,0x00,0x80,0x52,0x6d,0xfe,0xff,0x97};
inline constexpr std::array<unsigned char,36> BuildNote{
    0x04,0,0,0,0x14,0,0,0,3,0,0,0,0x47,0x4e,0x55,0,
    0xe5,0x85,0xdb,0x4a,0xc0,0xed,0x0e,0x5c,0x5e,0xcc,0x8c,0x79,0x76,0x8d,0x6b,0x38,0x18,0x2f,0x79,0x0e};
struct MemoryRecord {
    std::uint32_t magic{0x41524d34}, version{1}, phase{}, reserved{};
    std::uint64_t now_ms{}, pid{}, base{};
    std::uint32_t init_result{NotAttempted}, query_result{NotAttempted}, info_result{NotAttempted};
    std::uint32_t identity_matches{}, syscall_hints{}, module_hash_verified{};
    std::uint32_t memory_query_result{NotAttempted}, regions{}, candidates{}, code_matches{};
    std::uint32_t code_map_result{NotAttempted}, code_unmap_result{NotAttempted};
    std::uint32_t note_map_result{NotAttempted}, note_unmap_result{NotAttempted};
    std::uint32_t record_map_result{NotAttempted}, record_unmap_result{NotAttempted};
    std::uint32_t fingerprint_valid{}, build_note_valid{}, record_copied{}, record_coherent{};
    std::uint32_t state_result{NotAttempted}, state_valid{};
    std::int64_t state{-1};
    StartupRecord startup{};
    std::uint32_t close_result{NotAttempted}, startup_valid{}, reserved2{}, checksum{};
};
static_assert(sizeof(MemoryRecord) == 192 && std::is_trivially_copyable_v<MemoryRecord>);
inline std::uint32_t MemoryChecksum(const MemoryRecord &record) {
    const auto *p=reinterpret_cast<const unsigned char *>(&record); std::uint32_t h=2166136261u;
    for (std::size_t i=0; i<offsetof(MemoryRecord,checksum); ++i) h=(h^p[i])*16777619u;
    return h;
}
inline bool ValidMemoryRecord(const MemoryRecord &r) {
    return r.magic==0x41524d34 && r.version==1 && (r.phase==1 || r.phase==2) && !r.reserved && !r.reserved2 &&
        r.now_ms && r.checksum==MemoryChecksum(r) && r.startup_valid<=1 && r.record_copied<=1 &&
        (!r.record_copied || (r.fingerprint_valid && r.build_note_valid && r.identity_matches && r.module_hash_verified));
}
inline bool SaveMemoryExclusive(const char *path,const MemoryRecord &record) {
    if (!ValidMemoryRecord(record)) return false;
    FILE *f=std::fopen(path,"wbx"); if (!f) return false;
    const bool wrote=std::fwrite(&record,1,sizeof record,f)==sizeof record;
    const bool flushed=std::fflush(f)==0, synced=::fsync(::fileno(f))==0, closed=std::fclose(f)==0;
    MemoryRecord copy{};
    return wrote && flushed && synced && closed && ReadExact(path,copy) && !std::memcmp(&record,&copy,sizeof record);
}
}
