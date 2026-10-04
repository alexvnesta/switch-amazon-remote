// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "native_hdls.hpp"
#include "protocol.hpp"
#include "navigation_identity.hpp"
#include "launch_fix_sdk.hpp"
namespace armodule {
inline constexpr char NavigationAttemptPath[]="sdmc:/config/amazon-remote-nav9/launch.bin";
inline constexpr char NavigationAttemptTemp[]="sdmc:/config/amazon-remote-nav9/launch.tmp";
inline constexpr char LinkPendingPath[]="sdmc:/config/amazon-remote/link-test-pending.txt";
inline bool NavigationAbsent(const char *path) {
    struct stat info{}; errno=0;
    return ::stat(path,&info)!=0 && errno==ENOENT;
}
inline bool PriorNativePassed(const NativeResult &r) {
    const auto &t=r.trace;
    return ValidNativeResult(r) && r.stage==3 && !r.result && !r.heap_result && !r.sm_result &&
        !r.fs_result && !r.mount_result && !r.firmware_result && t.allocation_ok &&
        !t.open_result && !t.memory_query_result && !t.attach_session_result && !t.attach_device_result &&
        !t.first_set_result && !t.last_set_result && !t.detach_result && !t.release_result &&
        t.set_calls==2 && t.closed && t.freed && !t.quarantined;
}
inline bool VerifyNavigationBinary(std::array<std::uint8_t,32> &hash) {
    if (!NavigationModuleSize) return false;
    FILE *file=std::fopen("sdmc:/atmosphere/contents/010000000000a4d3/exefs.nsp","rb"); if (!file) return false;
    Sha256Context sha{}; sha256ContextCreate(&sha); std::size_t total=0; unsigned char buffer[4096]{};
    while (total<NavigationModuleSize) {
        const auto left=NavigationModuleSize-total;
        const auto amount=std::fread(buffer,1,left<sizeof buffer?left:sizeof buffer,file); if (!amount) break;
        sha256ContextUpdate(&sha,buffer,amount); total+=amount;
    }
    const bool exact=total==NavigationModuleSize && std::fgetc(file)==EOF && !std::ferror(file);
    const bool closed=std::fclose(file)==0;
    if (!exact || !closed) return false;
    sha256ContextGetHash(&sha,hash.data()); return hash==NavigationModuleHash;
}
// Called only after a physical A edge followed by one second with ALL buttons
// released. Launch starts IDLE: a subsequent physical A and ZL are still needed.
// Never removes an attempt marker, clears bonds, changes radio state or retries.
inline const char *RequestNavigationLaunch(NativeAttempt &r) {
    r={}; r.magic=0x41524c39;
    for (auto path:{NavigationAttemptPath,NavigationAttemptTemp,LinkPendingPath})
        if (!NavigationAbsent(path)) return "REFUSED: prior attempt/lock exists. Return to FTPD; no retry.";
    NativeResult prior{};
    if (!ReadExact(NativeResultPath,prior) || !PriorNativePassed(prior))
        return "REFUSED: successful native-pad evidence missing. Return to FTPD.";
    // A deliberately conservative admission guard, not a cryptographic boot ID.
    // Root also requires operator reboot confirmation before recovering the lock.
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    if (!now || now>=prior.now_ms) return "REFUSED: fresh reboot not established. Return to FTPD.";
    r.query_init=pmdmntInitialize(); u64 pid{};
    if (!r.query_init) {
        for (auto id:{SafeModuleProgramId,NativeHdlsProgramId,ModuleProgramId}) {
            pid=0; r.query_result=pmdmntGetProcessId(&pid,id);
            if (r.query_result!=0x20f || pid) break;
        }
        pmdmntExit();
    }
    if (r.query_init || r.query_result!=0x20f || pid)
        return "REFUSED: old/new native process absence not proven.";
    if (!VerifyNavigationBinary(r.module_sha256)) return "REFUSED: exact navigation binary not verified.";
    r.issued_ms=armTicksToNs(armGetSystemTick())/1000000; r.stage=1; r.hash_verified=r.buttons_released=1;
    r.checksum=NativeChecksum(r);
    if (!r.issued_ms || !FixSaveExclusive(NavigationAttemptPath,r)) return "REFUSED: durable launch intent failed.";
    r.shell_result=pmshellInitialize();
    if (!r.shell_result) {
        NcmProgramLocation location{}; location.program_id=ModuleProgramId; location.storageID=NcmStorageId_None;
        r.launch_result=pmshellLaunchProgram(PmLaunchFlag_None,&location,&pid); r.returned_pid=pid;
        pmshellExit(); r.stage=3;
    } else r.stage=2;
    r.returned_ms=armTicksToNs(armGetSystemTick())/1000000; r.checksum=NativeChecksum(r);
    if (!Publish(NavigationAttemptTemp,NavigationAttemptPath,r)) return "Receipt failed. Photograph screen; no retry.";
    return "Launch returned. Wait for IDLE; release, then A to arm. No automatic Bluetooth.";
}
}
