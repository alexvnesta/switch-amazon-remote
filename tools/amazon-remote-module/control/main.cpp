// SPDX-License-Identifier: GPL-2.0-only
// Only writes bounded operator commands. No Bluetooth, pairing or HDLS here.
#include <switch.h>
#include "../protocol.hpp"
#include "../io.hpp"
#include "../startup.hpp"
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11)
#include "../navigation_sdk.hpp"
#endif
#include <cstdio>
#include <cstring>
#include <limits>
namespace {
std::uint64_t Now() { return armTicksToNs(armGetSystemTick()) / 1000000; }
const char *Name(std::uint32_t state) {
    constexpr const char *names[] = {"IDLE", "SCANNING", "LINKING", "READY", "STOPPED", "SUSPENDED"};
    return state < 6 ? names[state] : "INVALID";
}
}
int main(int, char **) {
    consoleInit(nullptr);
    PadState pad; padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    std::uint64_t last_send = 0, last_draw = 0;
    std::uint64_t last_query = 0;
    armodule::StartupControlReport diagnostic{}, saved{};
    unsigned report_writes = 0; bool report_saved = false;
    bool released = false; const char *message = "No command sent. Module must be deliberately enabled first.";
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11)
    armodule::ReleasedLaunchGate launch_gate;
    armodule::NativeAttempt launch{};
    message = "Release all buttons. A once, release ALL buttons for one second to launch IDLE.";
#endif
    while (appletMainLoop()) {
        padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (!(padGetButtons(&pad) & (HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_X | HidNpadButton_Y))) released = true;
        armodule::Status status{};
        bool fresh = armodule::ReadExact(armodule::StatusPath, status) && armodule::FreshStatus(status, Now());
        if (!last_query || Now()-last_query >= 1000) {
            diagnostic = {}; diagnostic.now_ms = Now();
            auto result = pmdmntInitialize();
            if (R_SUCCEEDED(result)) { result = pmdmntGetProcessId(&diagnostic.process_id, armodule::ModuleProgramId); pmdmntExit(); }
            diagnostic.process_query_result = result;
            diagnostic.startup_valid = armodule::ReadExact(armodule::StartupPath, diagnostic.startup) && armodule::ValidStartup(diagnostic.startup);
            if (!diagnostic.startup_valid) diagnostic.startup = {};
            // Bounded, module-owned evidence for FTP retrieval; no launch API.
            const bool changed = !report_writes || diagnostic.process_query_result != saved.process_query_result ||
                diagnostic.process_id != saved.process_id || diagnostic.startup_valid != saved.startup_valid ||
                std::memcmp(&diagnostic.startup, &saved.startup, sizeof diagnostic.startup);
            if (changed && report_writes < 20) {
                ++report_writes;
                report_saved = armodule::Publish(armodule::ControlTemp, armodule::ControlPath, diagnostic);
                saved = diagnostic;
            }
            last_query = diagnostic.now_ms;
        }
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11)
        // Separate namespace plus a live PM query and matching early checkpoint.
        fresh = fresh && !diagnostic.process_query_result && diagnostic.process_id &&
            diagnostic.startup_valid && !diagnostic.startup.result &&
            status.instance==diagnostic.startup.instance && diagnostic.startup.now_ms<=status.now_ms;
        if (!fresh && launch_gate.Poll(Now(), down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A,
                                      padGetButtons(&pad)==0)) {
            message=armodule::RequestNavigationLaunch(launch);
        }
#endif
        const auto requested = down & (HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_X | HidNpadButton_Y);
        if (requested && (released || (requested & HidNpadButton_X))) {
            // X is the stop command and wins over other simultaneous buttons.
            auto kind = (requested & HidNpadButton_X) ? armodule::CommandKind::Stop :
                (requested & HidNpadButton_A) ? armodule::CommandKind::Arm :
                (requested & HidNpadButton_Y) ? armodule::CommandKind::CheckController : armodule::CommandKind::Link;
            const bool allowed = fresh &&
                (!armodule::StartupDiagnosticOnly || kind == armodule::CommandKind::Stop || kind == armodule::CommandKind::CheckController) &&
                (kind == armodule::CommandKind::Stop ||
                 (kind == armodule::CommandKind::Arm && status.state == unsigned(armodule::State::Idle)) ||
                 (kind == armodule::CommandKind::CheckController && status.state == unsigned(armodule::State::Idle) &&
                  status.controller_check == unsigned(armodule::ControllerCheck::NotRun)) ||
                 (kind == armodule::CommandKind::Link && status.state == unsigned(armodule::State::Scanning) && status.link_ready));
            if (!allowed) message = "Ignored: stale/missing module, wrong state or link not READY. Not queued.";
            else {
                const auto previous = status.last_sequence > last_send ? status.last_sequence : last_send;
                if (previous == std::numeric_limits<std::uint64_t>::max()) message = "Command counter exhausted. No write.";
                else {
                    armodule::Command command{}; command.kind = unsigned(kind); command.instance = status.instance;
                    command.issued_ms = Now(); command.sequence = previous+1;
                    if (armodule::Publish(armodule::CommandTemp, armodule::CommandPath, command)) {
                        last_send = command.sequence; message = "Command published. Check state/log for actual acknowledgement.";
                    } else message = "Command publication failed. Existing temp file is preserved.";
                }
            }
        }
        if (requested) released = false; // Require a fresh release before another physical command.
        if (!last_draw || Now()-last_draw >= 250 || requested) {
            consoleClear();
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11)
#ifdef ARMODULE_NAV11
            std::printf("Amazon Remote Navigation 11 - owned first status write\n\n");
#elif defined(ARMODULE_NAV10)
            std::printf("Amazon Remote Navigation 10 - system BTM / status checksum\n\n");
#else
            std::printf("Amazon Remote Navigation 9 - supervised test\n\n");
#endif
            std::printf("No status: A once, release ALL buttons1s to launch IDLE\nIDLE: release, A arms60s capture; flash remote\nSCANNING and Link_READY=1: ZL once connects\nX: stop/release pad   +: exit UI (module remains)\n\n");
#else
            std::printf("Amazon Remote Startup Check - alpha.2 diag1\n\n");
            std::printf("Y: one NEUTRAL pad attach/detach check (IDLE, no Bluetooth)\nA/ZL: DISABLED for startup diagnosis\nX: stop/release virtual controller   +: exit UI\n\n");
#endif
            if (!diagnostic.process_query_result && diagnostic.process_id)
                std::printf("PM: module process PRESENT, PID=%llu (not a health check)\n", static_cast<unsigned long long>(diagnostic.process_id));
            else std::printf("PM: no process confirmed, Result=%08lx\n", static_cast<unsigned long>(diagnostic.process_query_result));
            if (diagnostic.startup_valid) std::printf("Saved startup: %s Result=%08lx\nCheckpoint belongs to live status instance: %s\n",
                armodule::StartupName(diagnostic.startup.stage), static_cast<unsigned long>(diagnostic.startup.result),
                fresh && status.instance == diagnostic.startup.instance ? "YES" : "UNCONFIRMED (may be an earlier boot)");
            else std::printf("No valid early SD checkpoint. Loader/SM/FS remain unresolved.\n");
            std::printf("FTP diagnostic snapshot: %s\n\n", report_saved ? "SAVED" : "NOT SAVED");
            if (fresh) {
                constexpr const char *checks[] = {"NOT RUN", "PASSED (service replies only)", "FAILED"};
                std::printf("State=%s Result=%08lx Link_READY=%u Pad_attached=%u\nRemote_buttons=%08lx Acknowledged_sequence=%llu\n",
                Name(status.state), static_cast<unsigned long>(status.result), status.link_ready, status.output_attached,
                static_cast<unsigned long>(status.buttons), static_cast<unsigned long long>(status.last_sequence));
                std::printf("Neutral_check=%s Pad_cleanup_result=%08lx\nLast_ACK_mask=%016llx Successful_Set_replies=%llu\n",
                    checks[status.controller_check], static_cast<unsigned long>(status.pad_cleanup_result),
                    static_cast<unsigned long long>(status.acknowledged_buttons), static_cast<unsigned long long>(status.successful_writes));
            }
            else std::printf("No current module status. No Bluetooth action permitted.\n");
            std::printf("\n%s\n\n", message);
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11)
            std::printf("One attempt,90s connected limit. No reconnect/autoboot.\nAfter READY: + exits; try D-pad/OK/Back on HOME.\nNo wake or microphone support is claimed.\n");
#else
            std::printf("Startup diagnosis only. No Bluetooth/pairing commands.\nNo wake or microphone support is claimed.\n");
#endif
            last_draw = Now();
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
