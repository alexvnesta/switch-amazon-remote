// SPDX-License-Identifier: GPL-2.0-only
#include <switch_buttons.hpp> // Actual pinned public MissionControl file.
#include "../mc_mitm/source/amazon_remote/missioncontrol_buttons.hpp"
#include "../mc_mitm/source/amazon_remote/k7q3m7_descriptor.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace amazon_remote;
using ams::controller::SwitchButtons;
using ams::controller::SwitchButton;
int main() {
    struct Mapping {std::uint32_t remote;SwitchButton target;std::uint32_t bit;};
    constexpr Mapping mappings[]={{Up,SwitchButton::Up,1u<<17},{Down,SwitchButton::Down,1u<<16},
        {Left,SwitchButton::Left,1u<<19},{Right,SwitchButton::Right,1u<<18},
        {Confirm,SwitchButton::A,1u<<3},{Back,SwitchButton::B,1u<<2},
        {Home,SwitchButton::Home,1u<<12},{Menu,SwitchButton::Plus,1u<<9}};
    for (std::uint32_t mask=0;mask<65536;++mask) {
        const auto mapped=MissionControlButtons<SwitchButtons,SwitchButton>(mask);
        std::uint32_t expected=0;
        for (auto m:mappings) {assert(mapped.Test(m.target)==bool(mask&m.remote));if(mask&m.remote)expected|=m.bit;}
        assert(mapped.Value()==expected);
        const auto bytes=mapped.GetState();
        assert((std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16))==expected);
    }
    // Real shared decoder, report IDs supplied by validated GATT references.
    RemoteDecoder decoder;assert(decoder.Configure(K7q3m7Descriptor,sizeof K7q3m7Descriptor));
    const std::uint8_t keyboard[]={0x52,0x58,0},consumer[]={0x23,2,0,0},release_keyboard[]={0,0,0},release_consumer[]={0,0,0,0};
    assert(decoder.Ingest(1,keyboard,sizeof keyboard)==Update::Changed);
    assert(decoder.Ingest(2,consumer,sizeof consumer)==Update::Changed);
    auto mapped=MissionControlButtons<SwitchButtons,SwitchButton>(decoder.Buttons());
    assert(mapped.Test(SwitchButton::Up) && mapped.Test(SwitchButton::A) && mapped.Test(SwitchButton::Home));
    assert(decoder.Ingest(1,release_keyboard,sizeof release_keyboard)==Update::Changed);
    mapped=MissionControlButtons<SwitchButtons,SwitchButton>(decoder.Buttons());
    assert(mapped.Value()==(1u<<12)); // Separate consumer hold survives keyboard release.
    assert(decoder.Ingest(2,release_consumer,sizeof release_consumer)==Update::Changed);
    assert((MissionControlButtons<SwitchButtons,SwitchButton>(decoder.Buttons()).None()));
    assert(decoder.Ingest(1,keyboard,sizeof keyboard)==Update::Changed);
    assert(decoder.Disconnect());
    assert((MissionControlButtons<SwitchButtons,SwitchButton>(decoder.Buttons()).None()));
    std::puts("PASS65536 masks and shared-decoder keyboard/consumer releases against actual pinned MissionControl SwitchButtons. No device connection/report injection proof.");
}
