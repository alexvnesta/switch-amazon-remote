// SPDX-License-Identifier: GPL-2.0-only
// Only writes bounded operator commands. No Bluetooth, pairing or HDLS here.
#include <switch.h>
#include "../protocol.hpp"
#include "../io.hpp"
#include <cstdio>
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
    bool released = false; const char *message = "No command sent. Module must be deliberately enabled first.";
    while (appletMainLoop()) {
        padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (!(padGetButtons(&pad) & (HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_X | HidNpadButton_Y))) released = true;
        armodule::Status status{};
        const bool fresh = armodule::ReadExact(armodule::StatusPath, status) && armodule::FreshStatus(status, Now());
        const auto requested = down & (HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_X | HidNpadButton_Y);
        if (requested && (released || (requested & HidNpadButton_X))) {
            // X is the stop command and wins over other simultaneous buttons.
            auto kind = (requested & HidNpadButton_X) ? armodule::CommandKind::Stop :
                (requested & HidNpadButton_A) ? armodule::CommandKind::Arm :
                (requested & HidNpadButton_Y) ? armodule::CommandKind::CheckController : armodule::CommandKind::Link;
            const bool allowed = fresh &&
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
                    if (armodule::Publish("sdmc:/config/amazon-remote-module/command.tmp", armodule::CommandPath, command)) {
                        last_send = command.sequence; message = "Command published. Check state/log for actual acknowledgement.";
                    } else message = "Command publication failed. Existing temp file is preserved.";
                }
            }
        }
        if (requested) released = false; // Require a fresh release before another physical command.
        if (!last_draw || Now()-last_draw >= 250 || requested) {
            consoleClear();
            std::printf("Amazon Remote Module Control 0.2.0-alpha.2\n\n");
            std::printf("Y: one NEUTRAL pad attach/detach check (IDLE, no Bluetooth)\nA: one capture/scan per boot   ZL: one fresh-target link\nX: stop/release virtual controller   +: exit UI (module continues)\n\n");
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
            std::printf("90-second bounded input test; no automatic reconnect.\nReboot/recovery required after EVERY attempted link.\nNo wake or microphone support is claimed.\n");
            last_draw = Now();
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
