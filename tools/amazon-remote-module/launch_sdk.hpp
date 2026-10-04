// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "launch_test.hpp"
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
        constexpr char path[] = "sdmc:/atmosphere/contents/0100000000a4d001/exefs.nsp";
        constexpr std::size_t expected_size = 142150;
        FILE *file = std::fopen(path, "rb"); if (!file) return false;
        Sha256Context sha{}; sha256ContextCreate(&sha);
        std::size_t total = 0; unsigned char buffer[4096]{};
        while (total < expected_size) {
            const auto remaining = expected_size-total;
            const auto amount = std::fread(buffer, 1, remaining < sizeof buffer ? remaining : sizeof buffer, file);
            if (!amount) break;
            sha256ContextUpdate(&sha, buffer, amount); total += amount;
        }
        const bool exact = total == expected_size && std::fgetc(file) == EOF && !std::ferror(file);
        const bool closed = std::fclose(file) == 0;
        if (!exact || !closed) return false;
        sha256ContextGetHash(&sha, hash.data()); return hash == LaunchModuleHash;
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
