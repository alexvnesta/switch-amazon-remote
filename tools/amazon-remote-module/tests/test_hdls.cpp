// SPDX-License-Identifier: GPL-2.0-only
#include "../hdls_backend.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
namespace {
unsigned fail_step{}, calls{};
std::vector<unsigned> order;
std::vector<std::uint64_t> outputs;
Result Call(unsigned operation) { order.push_back(operation); return ++calls == fail_step ? 42 : 0; }
void Reset(unsigned failure) { fail_step=failure; calls=0; order.clear(); outputs.clear(); }
}
Result hiddbgInitialize() { return Call(1); }
Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId *out, void *buffer, std::size_t size) {
    assert(size == 4096 && reinterpret_cast<std::uintptr_t>(buffer) % 4096 == 0);
    const auto result=Call(2); if (!result) out->id=17; return result;
}
Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle *out, const HiddbgHdlsDeviceInfo *info) {
    assert(info->deviceType == HidDeviceType_FullKey15 && info->npadInterfaceType == HidNpadInterfaceType_USB);
    const auto result=Call(3); if (!result) out->handle=29; return result;
}
Result hiddbgSetHdlsState(HiddbgHdlsHandle handle, const HiddbgHdlsState *state) {
    assert(handle.handle == 29 && state->battery_level == 4 && state->flags == 1);
    assert(!state->analog_stick_l.x && !state->analog_stick_l.y && !state->analog_stick_r.x && !state->analog_stick_r.y);
    outputs.push_back(state->buttons); return Call(4);
}
Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle handle) { assert(handle.handle==29); return Call(5); }
Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId session) { assert(session.id==17); return Call(6); }
void hiddbgExit() { Call(7); }
int main() {
    for (unsigned failure=0; failure<=7; ++failure) {
        Reset(failure);
        armodule::HdlsBackend backend; armodule::OwnedController controller(backend);
        assert(armodule::NeutralControllerCheck(controller) == (failure ? 42u : 0u));
        assert(!controller.Attached());
        for (auto buttons : outputs) assert(!buttons);
        if (!failure) assert((order == std::vector<unsigned>{1,2,3,4,4,5,6,7}));
    }
    Reset(0);
    armodule::HdlsBackend backend; armodule::OwnedController controller(backend);
    assert(controller.Start()==0);
    assert(controller.Update(amazon_remote::Confirm | amazon_remote::Home)==0);
    assert(outputs.back() == (HidNpadButton_A | HiddbgNpadButton_Home));
    const auto count=calls;
    assert(controller.Update(amazon_remote::Confirm | amazon_remote::Home)==0 && calls==count);
    assert(controller.Update(0)==0 && outputs.back()==0);
    assert(controller.Stop()==0 && !controller.Attached());
    std::puts("PASS actual HDLS adapter: aligned owned buffer, virtual-device request, neutral preflight/failures, mapped press-release and cleanup. NOT hardware proof.");
}
