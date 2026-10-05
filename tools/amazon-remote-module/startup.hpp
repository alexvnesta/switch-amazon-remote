// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace armodule {
// Temporary, supervised revisions, not public releases.
#ifdef ARMODULE_NAV15
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d9;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav15/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav15/startup.bin";
#elif defined(ARMODULE_NAV14)
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d8;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav14/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav14/startup.bin";
#elif defined(ARMODULE_NAV13)
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d7;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav13/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav13/startup.bin";
#elif defined(ARMODULE_NAV12)
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d6;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav12/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav12/startup.bin";
#elif defined(ARMODULE_NAV11)
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d5;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav11/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav11/startup.bin";
#elif defined(ARMODULE_NAV10)
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d4;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav10/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav10/startup.bin";
#elif defined(ARMODULE_NAV9)
// Separately staged navigation candidate. No boot flag or automatic BLE action.
inline constexpr bool StartupDiagnosticOnly = false;
inline constexpr std::uint64_t ModuleProgramId = 0x010000000000a4d3;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-nav9/startup.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-nav9/startup.bin";
#else
inline constexpr bool StartupDiagnosticOnly = true;
inline constexpr std::uint64_t ModuleProgramId = 0x0100000000a4d001;
inline constexpr char StartupPath[] = "sdmc:/config/amazon-remote-module/startup-diag1.bin";
inline constexpr char StartupFsPath[] = "/config/amazon-remote-module/startup-diag1.bin";
#endif
enum class StartupStage : std::uint32_t {
    Sm = 1, Fs, Sd, Mount, Settings, Firmware, Main, Enable, Log,
    FirmwareGuard, Psc, PscRegister, Status, Exit
};
struct StartupRecord {
    std::uint32_t magic{0x41525331}, version{1}, stage{}, result{};
    std::uint64_t instance{}, now_ms{};
    std::uint32_t reserved{}, checksum{};
};
static_assert(sizeof(StartupRecord) == 40 && std::is_trivially_copyable_v<StartupRecord>);
inline std::uint32_t StartupChecksum(const StartupRecord &value) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < offsetof(StartupRecord, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
inline bool ValidStartup(const StartupRecord &value) {
    return value.magic == 0x41525331 && value.version == 1 && !value.reserved &&
        value.stage >= unsigned(StartupStage::Sm) && value.stage <= unsigned(StartupStage::Exit) &&
        value.instance && value.now_ms && value.checksum == StartupChecksum(value);
}
inline const char *StartupName(std::uint32_t stage) {
    constexpr const char *names[] = {"INVALID", "SM", "FS", "SD open", "SD mount", "Settings",
        "Firmware read", "main entered", "Enable file", "Log open", "Firmware guard",
        "PSC init", "PSC register", "Status publish", "Exit"};
    return stage <= unsigned(StartupStage::Exit) ? names[stage] : names[0];
}
struct StartupControlReport {
    std::uint32_t magic{0x41524331}, version{1}, process_query_result{}, startup_valid{};
    std::uint64_t now_ms{}, process_id{};
    StartupRecord startup{};
};
static_assert(sizeof(StartupControlReport) == 72 && std::is_trivially_copyable_v<StartupControlReport>);
}
