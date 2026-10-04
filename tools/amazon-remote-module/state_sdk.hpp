// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "state_check.hpp"

namespace armodule {
// Getter ABI adapted from Atmosphere-NX, pm_ams.os.horizon.c (GPL-2.0):
// Pin: aae2da0155a490abd3bfd17a70ccd16bb73c916a.
// The command returns a COPY of PM's process handle, not a debug attachment.
struct ProcessInfoOutput {
    NcmProgramLocation location{};
    struct { u64 keys_held{}, flags{}; } override_status;
};
static_assert(sizeof(NcmProgramLocation) == 16 && sizeof(ProcessInfoOutput) == 32);
inline Result CopyProcessInfo(u64 pid, Handle &handle, ProcessInfoOutput &output) {
    SfDispatchParams params{};
    params.out_handle_attrs.attr0 = SfOutHandleAttr_HipcCopy;
    params.out_handles = &handle;
    return serviceDispatchImpl(pmdmntGetServiceSession(), 65000, &pid, sizeof pid,
        &output, sizeof output, params);
}
inline StateRecord CaptureProcessState(std::uint32_t phase) {
    StateRecord record{}; record.phase = phase;
    record.now_ms = armTicksToNs(armGetSystemTick()) / 1000000;
    record.init_result = pmdmntInitialize();
    if (R_SUCCEEDED(record.init_result)) {
        record.query_result = pmdmntGetProcessId(&record.pid, ModuleProgramId);
        if (R_SUCCEEDED(record.query_result) && record.pid) {
            constexpr std::size_t capacity = 32;
            u64 pids[capacity]{}; u32 count{};
            record.jit_result = pmdmntGetJitDebugProcessIdList(&count, pids, capacity);
            if (R_SUCCEEDED(record.jit_result)) {
                record.jit_count = count;
                // PM returns only the number copied, not the total. At capacity,
                // not-found is inconclusive. Never persist other processes' IDs.
                record.jit_at_capacity = count >= capacity;
                for (std::size_t i=0; i<count && i<capacity; ++i)
                    if (pids[i] == record.pid) record.jit_found = 1;
            }
            Handle handle = INVALID_HANDLE; ProcessInfoOutput info{};
            record.info_result = CopyProcessInfo(record.pid, handle, info);
            if (R_SUCCEEDED(record.info_result)) {
                record.location_program_id = info.location.program_id;
                record.identity_matches = info.location.program_id == ModuleProgramId;
                record.syscall_hinted = envIsSyscallHinted(0x7c);
                if (record.identity_matches && record.syscall_hinted && handle != INVALID_HANDLE) {
                    record.state_attempted = 1;
                    record.state_result = svcGetProcessInfo(&record.state, handle, ProcessInfoType_ProcessState);
                    record.state_valid = R_SUCCEEDED(record.state_result) && record.state >= 0 && record.state <= 7;
                }
            }
            if (handle != INVALID_HANDLE) {
                record.close_attempted = 1; record.close_result = svcCloseHandle(handle);
            }
        }
        pmdmntExit();
    }
    record.checksum = StateChecksum(record); return record;
}
}
