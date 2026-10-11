// SPDX-License-Identifier: GPL-2.0-only
// Status and stop only. Does not launch a process, arm, connect, or pair.
#include <switch.h>
#include "../protocol.hpp"
#include "../io.hpp"
#ifdef ARMODULE_AUTOMATIC_A2
#include "../current_status.hpp"
#endif
#include "operator_input.hpp"
#include <cstdio>
#include <limits>
namespace {
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
}
int main(int,char**) {
    consoleInit(nullptr);
    padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for(unsigned i=0;i<arsetupinput::SupportedPlayers;++i)padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;
    armodule::Status status{};
#ifdef ARMODULE_AUTOMATIC_A2
    armodule::StartupRecord startup{};bool startup_valid{};
#endif
    std::uint64_t last_read{},last_draw{},sent_sequence{},sent_instance{};
    bool valid{},stop_sent{},write_failed{};
    while(appletMainLoop()) {
        arsetupinput::Frame frame{};
        for(unsigned i=0;i<pads.size();++i) {
            padUpdate(&pads[i]);
            frame[i]={padGetStyleSet(&pads[i]),unsigned(pads[i].active_id_mask)|(unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};
        }
        const auto buttons=input.Update(frame);const auto now=Now();
        if(!last_read || now-last_read>=250) {
            valid=armodule::ReadExact(armodule::StatusPath,status) && armodule::FreshStatus(status,now);
#ifdef ARMODULE_AUTOMATIC_A2
            startup_valid=armodule::ReadExact(armodule::StartupPath,startup) && armodule::ValidStartup(startup) && startup.now_ms && startup.now_ms<=now;
            valid=valid && startup_valid && armodule::CurrentStatus(status,startup,now);
#endif
            last_read=now;
        }
        const bool fresh=valid && armodule::FreshStatus(status,now);
        if(buttons.down&HidNpadButton_Plus)break;
        if((buttons.down&HidNpadButton_X) && fresh && !stop_sent && !write_failed &&
           status.last_sequence<std::numeric_limits<std::uint64_t>::max()) {
            armodule::Command command{};command.kind=unsigned(armodule::CommandKind::Stop);
            command.instance=status.instance;command.issued_ms=now;command.sequence=status.last_sequence+1;
            stop_sent=armodule::Publish(armodule::CommandTemp,armodule::CommandPath,command);
            write_failed=!stop_sent;sent_sequence=command.sequence;sent_instance=command.instance;
        }
        if(!last_draw || now-last_draw>=250) {
            consoleClear();
#ifdef ARMODULE_AUTOMATIC_A2
#ifdef ARMODULE_SETUP_SLEEP_R3
            std::printf("AMAZON REMOTE  |  SETUP R3\n\n");
#elif defined(ARMODULE_HID_INFO_H1)
            std::printf("AMAZON REMOTE  |  HID INFO H1\n\n");
#elif defined(ARMODULE_LATENCY_A6)
            std::printf("AMAZON REMOTE  |  LATENCY A6\n\n");
#elif defined(ARMODULE_RETRY_A5)
            std::printf("AMAZON REMOTE  |  RETRY A5\n\n");
#elif defined(ARMODULE_STARTUP_A4)
            std::printf("AMAZON REMOTE  |  STARTUP A4\n\n");
#elif defined(ARMODULE_WAKE_A3)
            std::printf("AMAZON REMOTE  |  WAKE A3\n\n");
#else
            std::printf("AMAZON REMOTE  |  AUTO A2\n\n");
#endif
            if(!fresh && startup_valid && startup.result)
                std::printf("\x1b[31mSTARTUP ERROR\x1b[0m\n\nStage: %s  Result: %08lx\nOpen FTPD for a log check.\n",
                    armodule::StartupName(startup.stage),static_cast<unsigned long>(startup.result));
            else
#else
            std::printf("AMAZON REMOTE  |  AUTO A1\n\n");
#endif
            if(!fresh)std::printf("\x1b[33mNO CURRENT MODULE STATUS\x1b[0m\n\nOpen FTPD for a log check.\n");
            else if(status.state==unsigned(armodule::State::Ready) && status.output_attached && !status.result)
                std::printf("\x1b[32mCONNECTED\x1b[0m\n\nUse the remote on HOME.\n");
            else if(status.state==unsigned(armodule::State::Idle))
                std::printf("\x1b[33mSTARTING AUTOMATICALLY\x1b[0m\n\nNo A or ZL needed. Keep Switch awake.\n");
            else if(status.state==unsigned(armodule::State::Scanning) || status.state==unsigned(armodule::State::Linking))
                std::printf("\x1b[33mCONNECTING\x1b[0m\n\nTap remote Up. Do not hold Home.\n");
            else if(status.state==unsigned(armodule::State::Stopping))
                std::printf("\x1b[33mDISCONNECTING\x1b[0m\n\nWait for STOPPED.\n");
#ifdef ARMODULE_WAKE_A3
            else if(status.state==unsigned(armodule::State::Suspended))
                std::printf("\x1b[33mRESUMING AFTER SLEEP\x1b[0m\n\nKeep Switch awake and tap remote Up.\n");
#endif
            else std::printf("\x1b[31mSTOPPED\x1b[0m\n\nResult: %08lx  Cleanup: %08lx\nOpen FTPD for a log check.\n",
                static_cast<unsigned long>(status.result),static_cast<unsigned long>(status.pad_cleanup_result));
            if(write_failed)std::printf("\n\x1b[31mSTOP REQUEST NOT SAVED\x1b[0m\n");
            else if(stop_sent && (!fresh || status.instance!=sent_instance || status.last_sequence<sent_sequence))
                std::printf("\nStop requested; waiting for acknowledgement.\n");
#ifdef ARMODULE_WAKE_A3
#ifdef ARMODULE_RETRY_A5
            std::printf("\n----------------------------------------\nJoy-Con X: stop remote   +: exit\n\nEarly-link recovery: up to 3 requests.\nKeep awake while connecting. Do not re-pair.\n");
#else
            std::printf("\n----------------------------------------\nJoy-Con X: stop remote   +: exit\n\nSleep/wake preview: up to 8 sessions per boot.\nFailed attempts or X stop need reboot. No re-pair.\n");
#endif
#else
            std::printf("\n----------------------------------------\nJoy-Con X: stop remote   +: exit\n\nOne connection per boot in this preview.\nAfter sleep/link loss: log check, no re-pair.\n");
#endif
            last_draw=now;
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
