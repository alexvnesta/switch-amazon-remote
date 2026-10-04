// SPDX-License-Identifier: GPL-2.0-only
#include "../memory_sdk.hpp"
#include "../module_identity.hpp"
#include <cstdio>
namespace {
void Draw(const char *label,const armodule::MemoryRecord &r,bool saved) {
    std::printf("%s PID=%llu state=%lld valid=%u\n",label,static_cast<unsigned long long>(r.pid),static_cast<long long>(r.state),r.state_valid);
    std::printf("Hash=%u Identity=%u SVC-mask=%x Code=%u Build-ID=%u\n",r.module_hash_verified,r.identity_matches,r.syscall_hints,r.fingerprint_valid,r.build_note_valid);
    std::printf("PM init/query/copy=%08lx/%08lx/%08lx\n",static_cast<unsigned long>(r.init_result),static_cast<unsigned long>(r.query_result),static_cast<unsigned long>(r.info_result));
    std::printf("Maps code/note/record=%08lx/%08lx/%08lx\nUnmaps=%08lx/%08lx/%08lx\n",
        static_cast<unsigned long>(r.code_map_result),static_cast<unsigned long>(r.note_map_result),static_cast<unsigned long>(r.record_map_result),
        static_cast<unsigned long>(r.code_unmap_result),static_cast<unsigned long>(r.note_unmap_result),static_cast<unsigned long>(r.record_unmap_result));
    std::printf("RAM copied=%u coherent=%u checksum-valid=%u\n",r.record_copied,r.record_coherent,r.startup_valid);
    if (r.record_copied) {
        std::printf("Raw RAM: magic=%08lx stage=%u Result=%08lx\n",static_cast<unsigned long>(r.startup.magic),r.startup.stage,static_cast<unsigned long>(r.startup.result));
        if (r.startup_valid) std::printf("Last checkpoint: %s (not next-call proof)\n",armodule::StartupName(r.startup.stage));
        else if (r.record_coherent && r.startup.magic==0x41525331 && r.startup.version==1 && !r.startup.stage)
            std::printf("No completed SM checkpoint yet (stage0).\n");
    }
    std::printf("Saved+readback: %s\n\n",saved?"YES":"NO - photograph screen; preserve existing files");
}
}
int main(int,char **) {
    consoleInit(nullptr);
    PadState pad; padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    std::array<std::uint8_t,32> hash{};
    const auto first=armodule::CaptureMemoryCheckpoint(1,armodule::VerifyStartupModule(hash));
    const bool saved_first=armodule::SaveMemoryExclusive(armodule::MemoryOpenPath,first);
    armodule::MemoryRecord later{}; bool done{},saved_later{}; std::uint64_t draw{};
    while (appletMainLoop()) {
        padUpdate(&pad); if (padGetButtonsDown(&pad)&HidNpadButton_Plus) break;
        const auto now=armTicksToNs(armGetSystemTick())/1000000;
        if (!done && now-first.now_ms>=5000) {
            later=armodule::CaptureMemoryCheckpoint(2,armodule::VerifyStartupModule(hash)); done=true;
            saved_later=armodule::SaveMemoryExclusive(armodule::MemoryLaterPath,later);
        }
        if (!draw || now-draw>=250) {
            consoleClear();
            std::printf("Amazon Remote RAM Check - alpha.2 diag4\n\nNo reboot, relaunch, debugger attachment, patches or Bluetooth.\nWait 10 seconds, + exit, then open FTPD. No other buttons.\nffffffff = NOT ATTEMPTED. Running is not health proof.\n\n");
            Draw("On open",first,saved_first);
            if (done) Draw("After5s",later,saved_later); else std::printf("Waiting for second snapshot...\n");
            draw=now;
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
