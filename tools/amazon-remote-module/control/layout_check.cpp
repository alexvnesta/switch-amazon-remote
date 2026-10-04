// SPDX-License-Identifier: GPL-2.0-only
#include "../memory_sdk.hpp"
#include "../module_identity.hpp"
#include <cstdio>
namespace {
void Draw(const char *label,const armodule::LayoutRecord &layout,bool saved) {
    const auto &r=layout.report;
    std::printf("%s PID=%llu state=%lld valid=%u\n",label,static_cast<unsigned long long>(r.pid),static_cast<long long>(r.state),r.state_valid);
    std::printf("Layout count=%u complete=%u Candidates tried=%u\n",layout.count,layout.complete,r.candidates);
    std::printf("Image=%u Identity=%u SVC-mask=%x Code=%u Build-ID=%u\n",r.module_hash_verified,r.identity_matches,r.syscall_hints,r.fingerprint_valid,r.build_note_valid);
    std::printf("Code map/unmap=%08lx/%08lx Note=%08lx/%08lx\nRecord=%08lx/%08lx\n",
        static_cast<unsigned long>(r.code_map_result),static_cast<unsigned long>(r.code_unmap_result),
        static_cast<unsigned long>(r.note_map_result),static_cast<unsigned long>(r.note_unmap_result),
        static_cast<unsigned long>(r.record_map_result),static_cast<unsigned long>(r.record_unmap_result));
    std::printf("RAM copied=%u coherent=%u valid=%u\n",r.record_copied,r.record_coherent,r.startup_valid);
    if (r.startup_valid) std::printf("Last checkpoint=%s Result=%08lx\n",armodule::StartupName(r.startup.stage),static_cast<unsigned long>(r.startup.result));
    else if (r.record_copied) std::printf("Unvalidated RAM stage=%u magic=%08lx\n",r.startup.stage,static_cast<unsigned long>(r.startup.magic));
    std::printf("Saved+readback: %s\n\n",saved?"YES":"NO - photograph screen; preserve existing files");
}
}
int main(int,char **) {
    consoleInit(nullptr);
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    std::array<std::uint8_t,32> hash{};
    armodule::LayoutRecord first{},later{};
    (void)armodule::CaptureMemoryCheckpoint(1,armodule::VerifyStartupModule(hash),&first);
    const bool first_saved=armodule::SaveLayoutExclusive(armodule::LayoutOpenPath,first);
    bool done{},later_saved{};std::uint64_t draw{};
    while (appletMainLoop()) {
        padUpdate(&pad);if (padGetButtonsDown(&pad)&HidNpadButton_Plus) break;
        const auto now=armTicksToNs(armGetSystemTick())/1000000;
        if (!done && now-first.report.now_ms>=5000) {
            (void)armodule::CaptureMemoryCheckpoint(2,armodule::VerifyStartupModule(hash),&later);done=true;
            later_saved=armodule::SaveLayoutExclusive(armodule::LayoutLaterPath,later);
        }
        if (!draw || now-draw>=250) {
            consoleClear();
            std::printf("Amazon Remote Layout Check - alpha.2 diag5\n\nNo reboot, relaunch, debugger attachment, patches or Bluetooth.\nWait 10 seconds, + exit, then open FTPD. No other buttons.\nffffffff = NOT ATTEMPTED. Running is not health proof.\n\n");
            Draw("On open",first,first_saved);
            if (done) Draw("After5s",later,later_saved);else std::printf("Waiting for second snapshot...\n");
            draw=now;
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
