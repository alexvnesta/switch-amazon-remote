// SPDX-License-Identifier: GPL-2.0-only
#include "../launch_fix_sdk.hpp"
#include "../protocol.hpp"

namespace {
const char *Outcome(armodule::FixOutcome o) {
    using armodule::FixOutcome;
    switch (o) {
        case FixOutcome::LegacyNotHbl:return "REFUSED: matched legacy process HBL flag not confirmed.";
        case FixOutcome::SafeNotAbsent:return "REFUSED: new candidate absence not proven.";
        case FixOutcome::HashMismatch:return "REFUSED: pinned Bluetooth-disabled candidate not verified.";
        case FixOutcome::IntentFailed:return "REFUSED: durable intent failed/existing. Preserve all evidence.";
        case FixOutcome::ShellFailed:return "Shell init failed: exact Result saved. No launch.";
        case FixOutcome::Returned:return "Launch returned. Wait 10 seconds, then + and open FTPD.";
        case FixOutcome::ReceiptFailed:return "Receipt write failed. Photograph screen, +, open FTPD. No retry.";
    }
    return "Unknown";
}
void Show(const char *name,const armodule::OverrideRecord &r) {
    std::printf("%s: init=%08x query=%08x PID=%llu\ninfo=%08x close=%08x identity=%u HBL=%u\nlaunch-time keys=%016llx flags=%016llx\n",name,
        r.init_result,r.query_result,static_cast<unsigned long long>(r.pid),r.info_result,r.close_result,
        r.identity_matches,r.hbl,static_cast<unsigned long long>(r.keys_held),static_cast<unsigned long long>(r.flags));
}
}
int main(int,char **) {
    using namespace armodule;
    consoleInit(nullptr); PadState pad;
    padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    FixSdk sdk; ReleasedLaunchGate gate; FixRecord attempt{};
    const auto initial=sdk.Capture(false);
    const bool saved=ValidOverride(initial) && FixSaveExclusive(OverridePath,initial);
    struct stat info{}; errno=0;
    const bool prior=::stat(FixPath,&info)==0 || errno!=ENOENT;
    errno=0; const bool temp=::stat(FixTempPath,&info)==0 || errno!=ENOENT;
    bool allowed=saved && !prior && !temp && ConfirmedLegacyHbl(initial);
    const char *message=allowed ? "Press A ONCE, then RELEASE ALL buttons for one second." :
        "Launch disabled: missing HBL proof or previous/partial evidence. + then FTPD.";
    bool attempted=false,observed=false; std::uint64_t last_draw{};
    while (appletMainLoop()) {
        padUpdate(&pad); const auto down=padGetButtonsDown(&pad),held=padGetButtons(&pad);
        if (down&HidNpadButton_Plus) break;
        const auto now=sdk.Now();
        if (allowed && gate.Poll(now,down==HidNpadButton_A && held==HidNpadButton_A,held==0)) {
            allowed=false; attempted=true; message=Outcome(RequestFix(sdk,attempt));
        }
        if (attempted && !observed && attempt.stage==3 && attempt.returned_ms && now>=attempt.returned_ms && now-attempt.returned_ms>=5000) {
            observed=true; attempt.after=sdk.Capture(true); attempt.stage=4; attempt.checksum=FixChecksum(attempt);
            if (!sdk.Update(attempt)) message="5s receipt failed. Photograph screen and return to FTPD. No retry.";
        }
        if (!last_draw || now-last_draw>=250) {
            consoleClear(); std::printf("Amazon Remote Launch Fix - diag6\n\nNO REBOOT / NO PAIRING. + exit, then FTPD.\nBluetooth-disabled candidate; no boot flag/global override change.\n\n");
            Show("Legacy on open",initial);
            std::printf("Exclusive override evidence saved=%u\n\n",saved);
            if (attempted) {
                Show("Legacy at attempt",attempt.legacy); Show("Safe-ID before",attempt.before);
                std::printf("Stage=%u Shell=%08x Launch=%08x returned PID=%llu\n",attempt.stage,attempt.shell_result,
                    attempt.launch_result,static_cast<unsigned long long>(attempt.returned_pid));
                if (observed) Show("Safe-ID after 5s",attempt.after);
            }
            StartupRecord startup{}; Status status{};
            const bool checkpoint=ReadExact(StartupPath,startup) && ValidStartup(startup);
            const bool correlated=checkpoint && attempt.issued_ms && startup.now_ms>=attempt.issued_ms;
            if (checkpoint) std::printf("Startup=%s Result=%08x after this attempt=%u\n",StartupName(startup.stage),startup.result,correlated);
            else std::printf("No valid SD startup checkpoint yet.\n");
            const bool live=ReadExact(StatusPath,status) && FreshStatus(status,sdk.Now());
            std::printf("Fresh status=%u (PM/receipt alone is NOT module health)\n\n%s\n",live,message);
            last_draw=now;
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
