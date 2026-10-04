// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "launch_test.hpp"
#include "module_identity.hpp"
#include "io.hpp"
#include <cstring>

namespace armodule {
class LaunchSdk {
public:
    std::uint64_t Now() { return armTicksToNs(armGetSystemTick()) / 1000000; }
    std::uint32_t QueryProcess(std::uint64_t &pid) {
        auto rc = pmdmntInitialize();
        if (R_SUCCEEDED(rc)) { u64 native_pid{}; rc = pmdmntGetProcessId(&native_pid, ModuleProgramId); pid = native_pid; pmdmntExit(); }
        return rc;
    }
    bool VerifyModule(std::array<std::uint8_t,32> &hash) {
        return VerifyStartupModule(hash);
    }
    bool CreateIntent(const LaunchRecord &record) {
        // Exclusive creation is also a durable no-second-attempt marker. Any
        // old/partial receipt blocks another launch; never clear it from this UI.
        FILE *file = std::fopen(LaunchPath, "wbx"); if (!file) return false;
        const bool written = std::fwrite(&record, 1, sizeof record, file) == sizeof record;
        const bool flushed = std::fflush(file) == 0;
        const bool synced = ::fsync(::fileno(file)) == 0;
        const bool closed = std::fclose(file) == 0;
        LaunchRecord readback{};
        return written && flushed && synced && closed && ReadExact(LaunchPath, readback) &&
            ValidLaunchRecord(readback) && !std::memcmp(&readback, &record, sizeof record);
    }
    std::uint32_t InitializeShell() { return pmshellInitialize(); }
    std::uint32_t LaunchDiagnosticModule(std::uint64_t &pid) {
        // EXACT same location/zero flags as boot2's SD custom-module launch.
        // No debug hook, signal subscription, start-suspended or ASLR changes.
        NcmProgramLocation location{}; location.program_id = ModuleProgramId; location.storageID = NcmStorageId_None;
        u64 native_pid{}; const auto rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &native_pid);
        pid = native_pid; return rc;
    }
    void CloseShell() { pmshellExit(); }
    bool UpdateReceipt(const LaunchRecord &record) { return Publish(LaunchTempPath, LaunchPath, record); }
};
}
