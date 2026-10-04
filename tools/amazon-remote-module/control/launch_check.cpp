// SPDX-License-Identifier: GPL-2.0-only
// A separate supervised operator tool, never an auto-launcher or pairing app.
#include "../launch_sdk.hpp"
#include "../protocol.hpp"
#include <cstdio>
#include <cstring>

namespace {
const char *Outcome(armodule::LaunchOutcome outcome) {
    using armodule::LaunchOutcome;
    switch (outcome) {
        case LaunchOutcome::Ignored: return "No new launch (held/multiple buttons or attempt already consumed).";
        case LaunchOutcome::ProcessNotAbsent: return "REFUSED: process present or absence not proven. No launch.";
        case LaunchOutcome::BinaryNotVerified: return "REFUSED: exact Bluetooth-disabled module SHA256 not verified.";
        case LaunchOutcome::IntentNotSaved: return "REFUSED: durable intent not saved; existing/partial receipt preserved.";
        case LaunchOutcome::ServiceFailed: return "pm:shell initialization failed; exact Result saved. No launch.";
        case LaunchOutcome::LaunchReturned: return "Launch request returned; exact Result saved. Not a health proof.";
        case LaunchOutcome::FinalReceiptNotSaved: return "FINAL RECEIPT FAILED: intent preserved. Photograph these Results.";
    }
    return "Unknown outcome";
}
}
int main(int, char **) {
    consoleInit(nullptr);
    PadState pad; padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    armodule::LaunchSdk sdk; armodule::OperatorLaunchTest test;
    armodule::LaunchRecord attempt{}, previous{};
    bool prior_exists = false; struct stat prior{}; errno = 0;
    // Any existing entry or ambiguous stat error refuses another attempt.
    prior_exists = ::stat(armodule::LaunchPath, &prior) == 0 || errno != ENOENT;
    const bool prior_valid = prior_exists && armodule::ReadExact(armodule::LaunchPath, previous) && armodule::ValidLaunchRecord(previous);
    const char *message = prior_exists ? "Previous attempt exists: R disabled. Preserve receipt for review." : "No launch yet. Release buttons, then press R once deliberately.";
    std::uint64_t last_draw{}, last_poll{}, pid{}; std::uint32_t query_result{};
    armodule::StartupRecord startup{}; bool startup_valid{};
    armodule::Status status{}; bool status_fresh{};
    constexpr auto action_buttons = HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_A | HidNpadButton_B |
        HidNpadButton_X | HidNpadButton_Y | HidNpadButton_ZL | HidNpadButton_L;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const auto down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
        if (down & HidNpadButton_Plus) break;
        test.ObserveReleased(!(held & action_buttons));
        if (!prior_exists && (down & HidNpadButton_R)) {
            const auto outcome = test.Request((held & action_buttons) == HidNpadButton_R, sdk, attempt);
            message = Outcome(outcome); last_poll = 0;
        }
        const auto now = sdk.Now();
        if (!last_poll || now-last_poll >= 1000) {
            pid = 0; query_result = sdk.QueryProcess(pid);
            startup_valid = armodule::ReadExact(armodule::StartupPath, startup) && armodule::ValidStartup(startup);
            status_fresh = armodule::ReadExact(armodule::StatusPath, status) && armodule::FreshStatus(status, sdk.Now());
            last_poll = sdk.Now();
        }
        if (!last_draw || now-last_draw >= 250 || (down & HidNpadButton_R)) {
            consoleClear();
            std::printf("Amazon Remote Launch Check - alpha.2 diag2\n\n");
            std::printf("R (not ZR): ONE deliberate diagnostic-module launch\n+: exit, then open FTPD. No reboot required for this test.\nAll remote/pairing/pad-test commands are unavailable here.\n\n");
            std::printf("PM query=%08lx PID=%llu (getter, not module-health proof)\n", static_cast<unsigned long>(query_result), static_cast<unsigned long long>(pid));
            const auto &record = test.Consumed() ? attempt : previous;
            if (test.Consumed() || prior_valid) std::printf("Attempt stage=%u Before_query=%08lx\nShell_init=%08lx Launch_Result=%08lx Returned_PID=%llu\n",
                record.stage, static_cast<unsigned long>(record.before_query_result), static_cast<unsigned long>(record.service_init_result),
                static_cast<unsigned long>(record.launch_result), static_cast<unsigned long long>(record.returned_pid));
            if (startup_valid) std::printf("Saved startup=%s Result=%08lx (historical unless live instance matches)\n",
                armodule::StartupName(startup.stage), static_cast<unsigned long>(startup.result));
            else std::printf("No valid early SD checkpoint.\n");
            if (test.Consumed() && attempt.returned_ms && !attempt.observed_ms && sdk.Now()-attempt.returned_ms >= 5000) {
                attempt.observed_query_result = sdk.QueryProcess(attempt.observed_pid); attempt.observed_ms = sdk.Now();
                if (!sdk.UpdateReceipt(attempt)) message = "Post-launch snapshot write failed; earlier launch receipt preserved if publication failed before replacement.";
            }
            if ((test.Consumed() || prior_valid) && record.observed_ms) std::printf("5s observation: PM query=%08lx PID=%llu\n",
                static_cast<unsigned long>(record.observed_query_result), static_cast<unsigned long long>(record.observed_pid));
            std::printf("Fresh live status: %s\n\n%s\n", status_fresh ? "YES (not navigation proof)" : "NO", message);
            std::printf("Only the exact SHA256-pinned Bluetooth-disabled module is eligible.\nOne attempt receipt persists across reopening. No auto retry.\n");
            last_draw = sdk.Now();
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
