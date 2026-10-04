// SPDX-License-Identifier: GPL-2.0-only
#include "../native_hdls_sdk.hpp"
int main(int,char **) {
    using namespace armodule;
    consoleInit(nullptr); PadState pad; padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    ReleasedLaunchGate gate; NativeAttempt attempt{}; std::uint64_t last_draw{};
    bool allowed=NativeEvidenceAbsent();
    const char *message=allowed ? "Release all buttons, press A ONCE, then release everything." : "Previous evidence exists. No new test allowed. + then FTPD.";
    while (appletMainLoop()) {
        padUpdate(&pad); const auto down=padGetButtonsDown(&pad),held=padGetButtons(&pad);
        if (down&HidNpadButton_Plus) break;
        const auto now=armTicksToNs(armGetSystemTick())/1000000;
        if (allowed && gate.Poll(now,down==HidNpadButton_A && held==HidNpadButton_A,held==0)) {
            allowed=false; message=RequestNativeTest(attempt);
        }
        if (!last_draw || now-last_draw>=250) {
            consoleClear(); std::printf("Amazon Remote Native Pad Check - diag7\n\n");
            std::printf("A: one native launch + NEUTRAL attach/detach. +: exit.\nNo reboot, Bluetooth, pairing, navigation keys or boot changes.\n\n%s\n\n",message);
            std::printf("Intent stage=%u Query=%08x Shell=%08x Launch=%08x PID=%llu\n",attempt.stage,attempt.query_result,attempt.shell_result,attempt.launch_result,
                static_cast<unsigned long long>(attempt.returned_pid));
            NativeResult result{};
            if (ReadExact(NativeResultPath,result) && ValidNativeResult(result)) {
                std::printf("Native stage=%u Result=%08x Heap=%08x Firmware=%08x\n",result.stage,result.result,result.heap_result,result.firmware_result);
                if (result.stage==3) {
                    const auto &t=result.trace;
                    std::printf("Alloc=%u MemType=%u Perm=%u Attr=%u Query=%08x\nOpen=%08x Session=%08x Device=%08x\nSet(first/last)=%08x/%08x Calls=%u\nDetach=%08x Release=%08x Freed=%u Quarantined=%u\n",
                        t.allocation_ok,t.memory_type,t.permission,t.attributes,t.memory_query_result,t.open_result,t.attach_session_result,t.attach_device_result,
                        t.first_set_result,t.last_set_result,t.set_calls,t.detach_result,t.release_result,t.freed,t.quarantined);
                    std::printf("Service replies only, NOT remote/navigation support.\n");
                }
            } else std::printf("No valid native result yet. Wait10s, +, FTPD.\n");
            last_draw=now;
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
