// SPDX-License-Identifier: GPL-2.0-only
#include "../state_sdk.hpp"
#include <cstdio>

namespace {
void DrawRecord(const char *label, const armodule::StateRecord &record, bool saved) {
    std::printf("%s: %s\nPID=%llu, PM_init=%08lx Query=%08lx\n", label,
        armodule::ProcessStateName(record), static_cast<unsigned long long>(record.pid),
        static_cast<unsigned long>(record.init_result), static_cast<unsigned long>(record.query_result));
    std::printf("Copy=%08lx Identity=%u SVC_hinted=%u State_called=%u\nState_Result=%08lx Raw_state=%lld Close=%08lx\n",
        static_cast<unsigned long>(record.info_result), record.identity_matches, record.syscall_hinted,
        record.state_attempted, static_cast<unsigned long>(record.state_result),
        static_cast<long long>(record.state), static_cast<unsigned long>(record.close_result));
    std::printf("Exception-list Result=%08lx count=%u ours=%u capacity-bound=%u\nEvidence saved+readback: %s\n\n",
        static_cast<unsigned long>(record.jit_result), record.jit_count, record.jit_found,
        record.jit_at_capacity, saved ? "YES" : "NO - preserve files; photograph this screen");
}
}
int main(int, char **) {
    consoleInit(nullptr);
    PadState pad; padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    const auto first = armodule::CaptureProcessState(1);
    const bool first_saved = armodule::SaveStateExclusive(armodule::StateOpenPath, first);
    armodule::StateRecord later{}; bool later_done{}, later_saved{};
    std::uint64_t last_draw{};
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        const auto now = armTicksToNs(armGetSystemTick()) / 1000000;
        if (!later_done && now-first.now_ms >= 5000) {
            later = armodule::CaptureProcessState(2); later_done = true;
            later_saved = armodule::SaveStateExclusive(armodule::StateLaterPath, later);
        }
        if (!last_draw || now-last_draw >= 250) {
            consoleClear();
            std::printf("Amazon Remote State Check - alpha.2 diag3\n\nGetter-only: no launch, reboot, debugger attachment or Bluetooth.\nWait 10 seconds, + exit, then open FTPD. Do not pair.\nffffffff = NOT ATTEMPTED, not a returned service error.\nRunning may include waiting/blocked threads; it is not health proof.\n\n");
            DrawRecord("On open", first, first_saved);
            if (later_done) DrawRecord("After 5 seconds", later, later_saved);
            else std::printf("Waiting for second snapshot...\n");
            last_draw = now;
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
