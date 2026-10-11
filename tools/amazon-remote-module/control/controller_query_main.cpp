// SPDX-License-Identifier: GPL-2.0-only
// Q1/Q2 are withdrawn. Retain a harmless notice if accidentally rebuilt.
// No service session, report write, capture preparation or query is performed.
#include <switch.h>
#include "operator_input.hpp"
#include <cstdio>
int main(int,char **) {
    consoleInit(nullptr);
    padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for(unsigned i=0;i<arsetupinput::SupportedPlayers;++i) padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;
    std::printf("\n\n  \x1b[31;1mCAPABILITY TEST WITHDRAWN\x1b[0m\n\n\n"
                "  No Bluetooth command will be sent.\n\n\n"
                "  \x1b[33;1mPress + to exit.\x1b[0m\n");
    while(appletMainLoop()) {
        arsetupinput::Frame frame{};
        for(unsigned i=0;i<pads.size();++i) {
            padUpdate(&pads[i]);
            frame[i]={padGetStyleSet(&pads[i]),unsigned(pads[i].active_id_mask)|
                (unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};
        }
        if(input.Update(frame).down&HidNpadButton_Plus) break;
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
