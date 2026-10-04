// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "native_hdls.hpp"
#include "native_identity.hpp"
#include "launch_fix_sdk.hpp"
namespace armodule {
inline bool NativeEvidenceAbsent() {
    for (auto path:{NativePermitPath,NativeUsedPath,NativeAttemptPath,NativeAttemptTemp,NativeResultPath,NativeResultTemp}) {
        struct stat info{}; errno=0;
        if (::stat(path,&info)==0 || errno!=ENOENT) return false;
    }
    return true;
}
inline bool VerifyNativeBinary(std::array<std::uint8_t,32> &hash) {
    if (!NativeModuleSize) return false;
    FILE *file=std::fopen("sdmc:/atmosphere/contents/010000000000a4d2/exefs.nsp","rb"); if (!file) return false;
    Sha256Context sha{}; sha256ContextCreate(&sha); std::size_t total=0; unsigned char buffer[4096]{};
    while (total<NativeModuleSize) {
        const auto left=NativeModuleSize-total;
        const auto amount=std::fread(buffer,1,left<sizeof buffer?left:sizeof buffer,file); if (!amount) break;
        sha256ContextUpdate(&sha,buffer,amount); total+=amount;
    }
    const bool exact=total==NativeModuleSize && std::fgetc(file)==EOF && !std::ferror(file);
    const bool closed=std::fclose(file)==0;
    if (!exact || !closed) return false;
    sha256ContextGetHash(&sha,hash.data()); return hash==NativeModuleHash;
}
inline const char *RequestNativeTest(NativeAttempt &r) {
    r={};
    if (!NativeEvidenceAbsent()) return "REFUSED: existing/partial evidence. Preserve; no retry.";
    r.query_init=pmdmntInitialize(); u64 pid{};
    if (!r.query_init) { r.query_result=pmdmntGetProcessId(&pid,NativeHdlsProgramId); pmdmntExit(); }
    if (r.query_init || r.query_result!=0x20f || pid) return "REFUSED: native test process absence not proven.";
    if (!VerifyNativeBinary(r.module_sha256)) return "REFUSED: exact native test binary not verified.";
    r.issued_ms=armTicksToNs(armGetSystemTick())/1000000; r.stage=1; r.hash_verified=r.buttons_released=1;
    r.checksum=NativeChecksum(r);
    if (!r.issued_ms || !FixSaveExclusive(NativeAttemptPath,r)) return "REFUSED: durable launch intent failed.";
    NativePermit permit{}; permit.issued_ms=r.issued_ms; permit.checksum=NativeChecksum(permit);
    if (!FixSaveExclusive(NativePermitPath,permit)) return "REFUSED: durable neutral-test permit failed.";
    r.shell_result=pmshellInitialize();
    if (!r.shell_result) {
        NcmProgramLocation location{}; location.program_id=NativeHdlsProgramId; location.storageID=NcmStorageId_None;
        r.launch_result=pmshellLaunchProgram(PmLaunchFlag_None,&location,&pid); r.returned_pid=pid;
        pmshellExit(); r.stage=3;
    } else r.stage=2;
    r.returned_ms=armTicksToNs(armGetSystemTick())/1000000; r.checksum=NativeChecksum(r);
    if (!Publish(NativeAttemptTemp,NativeAttemptPath,r)) return "Receipt update failed: photograph Results. No retry.";
    return "Request returned. Wait 10 seconds; + exit; open FTPD. No retry.";
}
}
