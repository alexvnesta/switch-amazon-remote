// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "state_sdk.hpp"
#include "memory_check.hpp"

namespace armodule {
// Only our helper's address space is mapped. Source pages/permissions are never
// modified. All mapped pointers are const: no writes, patching or debug attach.
inline bool ReadDiagnosticPage(Handle process, u64 address, void *output, std::size_t size,
        Result &map_result, Result &unmap_result, bool *coherent=nullptr) {
    constexpr std::size_t page=4096;
    const auto offset=address&(page-1), source=address&~u64(page-1);
    if (!size || size>page-offset) return false;
    MemoryInfo info{}; u32 page_info{};
    map_result=svcQueryProcessMemory(&info,&page_info,process,source);
    if (map_result || info.addr>source || info.size<page || source-info.addr>info.size-page || info.attr ||
        (info.type!=MemType_CodeStatic && info.type!=MemType_CodeMutable) || !(info.perm & Perm_R)) return false;
    virtmemLock();
    void *alias=virtmemFindCodeMemory(page,page);
    if (!alias) { virtmemUnlock(); return false; }
    map_result=svcMapProcessMemory(alias,process,source,page);
    if (map_result) { virtmemUnlock(); return false; }
    const auto *bytes=static_cast<const volatile unsigned char *>(alias)+offset;
    auto *dest=static_cast<unsigned char *>(output);
    for (std::size_t i=0; i<size; ++i) dest[i]=bytes[i];
    if (coherent) {
        *coherent=true;
        for (std::size_t i=0; i<size; ++i) if (dest[i]!=bytes[i]) *coherent=false;
    }
    unmap_result=svcUnmapProcessMemory(alias,process,source,page);
    if (unmap_result) {
        // Do not release a failed-unmap range back to the allocator. Reserve it
        // until process exit, keep holding no source handle beyond inspection.
        (void)virtmemAddReservation(alias,page);
    }
    virtmemUnlock(); return !unmap_result;
}
inline MemoryRecord CaptureMemoryCheckpoint(std::uint32_t phase,bool module_hash_verified) {
    MemoryRecord r{}; r.phase=phase; r.now_ms=armTicksToNs(armGetSystemTick())/1000000;
    r.module_hash_verified=module_hash_verified;
    const unsigned required[]={0x74,0x75,0x76,0x7c};
    for (unsigned i=0;i<4;++i) if (envIsSyscallHinted(required[i])) r.syscall_hints|=1u<<i;
    if (!module_hash_verified || r.syscall_hints!=15) { r.checksum=MemoryChecksum(r); return r; }
    r.init_result=pmdmntInitialize();
    if (!r.init_result) {
        r.query_result=pmdmntGetProcessId(&r.pid,ModuleProgramId);
        Handle process=INVALID_HANDLE; ProcessInfoOutput output{};
        if (!r.query_result && r.pid) {
            r.info_result=CopyProcessInfo(r.pid,process,output);
            r.identity_matches=!r.info_result && output.location.program_id==ModuleProgramId;
            if (r.identity_matches && process!=INVALID_HANDLE) {
                r.state=0; r.state_result=svcGetProcessInfo(&r.state,process,ProcessInfoType_ProcessState);
                r.state_valid=!r.state_result && r.state>=0 && r.state<=7;
                // Enumerate metadata only, bounded to128 regions/39-bit space.
                // Read only code with the exact text extent, then verify both
                // fixed code bytes and GNU build note before reading the record.
                u64 address=0; constexpr u64 limit=1ull<<39;
                for (;r.regions<128 && address<limit;) {
                    ++r.regions;
                    MemoryInfo info{}; u32 page_info{};
                    r.memory_query_result=svcQueryProcessMemory(&info,&page_info,process,address);
                    if (r.memory_query_result || info.addr>address || !info.size || info.addr>=limit ||
                        info.size>limit-info.addr || info.addr+info.size<=address) break;
                    address=info.addr+info.size;
                    if (info.type!=MemType_CodeStatic || info.perm!=(Perm_R|Perm_X) || info.size!=TextExtent || info.attr) continue;
                    ++r.candidates;
                    std::array<unsigned char,64> code{};
                    if (!ReadDiagnosticPage(process,info.addr+CodeOffset,code.data(),code.size(),r.code_map_result,r.code_unmap_result)) break;
                    if (code!=CodeFingerprint) continue;
                    ++r.code_matches; r.base=info.addr; r.fingerprint_valid=1;
                    std::array<unsigned char,36> note{};
                    if (!ReadDiagnosticPage(process,r.base+NoteOffset,note.data(),note.size(),r.note_map_result,r.note_unmap_result)) break;
                    r.build_note_valid=note==BuildNote;
                    if (!r.build_note_valid) break;
                    bool coherent{};
                    r.record_copied=ReadDiagnosticPage(process,r.base+RecordOffset,&r.startup,sizeof r.startup,
                        r.record_map_result,r.record_unmap_result,&coherent);
                    r.record_coherent=r.record_copied && coherent;
                    r.startup_valid=r.record_coherent && ValidStartup(r.startup);
                    break;
                }
            }
        }
        if (process!=INVALID_HANDLE) r.close_result=svcCloseHandle(process);
        pmdmntExit();
    }
    r.checksum=MemoryChecksum(r); return r;
}
}
