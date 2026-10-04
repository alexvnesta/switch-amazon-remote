// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "launch_fix.hpp"
#include "state_sdk.hpp"
#include "io.hpp"

namespace armodule {
template<class T> bool FixSaveExclusive(const char *path,const T &record) {
    FILE *file=std::fopen(path,"wbx"); if (!file) return false;
    const bool wrote=std::fwrite(&record,1,sizeof record,file)==sizeof record;
    const bool flushed=std::fflush(file)==0, synced=::fsync(::fileno(file))==0;
    const bool closed=std::fclose(file)==0;
    T saved{};
    return wrote && flushed && synced && closed && ReadExact(path,saved) &&
        !std::memcmp(&record,&saved,sizeof record);
}
inline OverrideRecord CaptureOverride(bool safe) {
    const auto target=safe ? SafeModuleProgramId : ModuleProgramId;
    OverrideRecord r{}; r.now_ms=armTicksToNs(armGetSystemTick())/1000000;
    r.init_result=pmdmntInitialize();
    if (R_SUCCEEDED(r.init_result)) {
        r.query_result=pmdmntGetProcessId(&r.pid,target);
        if (R_SUCCEEDED(r.query_result) && r.pid) {
            Handle handle=INVALID_HANDLE; ProcessInfoOutput info{};
            r.info_result=CopyProcessInfo(r.pid,handle,info);
            if (R_SUCCEEDED(r.info_result)) {
                r.location_program_id=info.location.program_id;
                r.identity_matches=info.location.program_id==target;
                r.keys_held=info.override_status.keys_held; r.flags=info.override_status.flags;
                // Raw flags always saved, but interpreted only for matched identity.
                r.hbl=r.identity_matches && (r.flags&1);
            }
            if (handle!=INVALID_HANDLE) r.close_result=svcCloseHandle(handle);
        }
        pmdmntExit();
    }
    r.checksum=FixChecksum(r); return r;
}
class FixSdk {
public:
    std::uint64_t Now() { return armTicksToNs(armGetSystemTick())/1000000; }
    OverrideRecord Capture(bool safe) { return CaptureOverride(safe); }
    bool Verify(std::array<std::uint8_t,32> &hash) {
        FILE *file=std::fopen(SafeModulePath,"rb"); if (!file) return false;
        Sha256Context sha{}; sha256ContextCreate(&sha);
        std::size_t total=0; unsigned char buffer[4096]{};
        while (total<SafeModuleSize) {
            const auto left=SafeModuleSize-total;
            const auto amount=std::fread(buffer,1,left<sizeof buffer?left:sizeof buffer,file);
            if (!amount) break;
            sha256ContextUpdate(&sha,buffer,amount); total+=amount;
        }
        const bool exact=total==SafeModuleSize && std::fgetc(file)==EOF && !std::ferror(file);
        const bool closed=std::fclose(file)==0;
        if (!exact || !closed) return false;
        sha256ContextGetHash(&sha,hash.data()); return hash==SafeModuleHash;
    }
    bool CreateIntent(const FixRecord &r) {
        // Partial or prior durable intent is never removed by this tool.
        struct stat info{}; errno=0;
        if (::stat(FixTempPath,&info)==0 || errno!=ENOENT) return false;
        return ValidFix(r) && FixSaveExclusive(FixPath,r);
    }
    std::uint32_t InitializeShell() { return pmshellInitialize(); }
    std::uint32_t Launch(std::uint64_t &pid) {
        NcmProgramLocation location{};
        location.program_id=SafeModuleProgramId; location.storageID=NcmStorageId_None;
        u64 native_pid{};
        const auto rc=pmshellLaunchProgram(PmLaunchFlag_None,&location,&native_pid);
        pid=native_pid; return rc;
    }
    void CloseShell() { pmshellExit(); }
    bool Update(const FixRecord &r) {
        return ValidFix(r) && Publish(FixTempPath,FixPath,r);
    }
};
}
