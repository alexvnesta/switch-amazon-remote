// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "remote_decoder.hpp"
namespace amazon_remote {
// Instantiate with MissionControl's actual SwitchButtons/SwitchButton types.
// Its report-bit layout differs from libnx HDLS; never cast the HDLS mask.
template<class Buttons,class Button> constexpr Buttons MissionControlButtons(std::uint32_t remote) {
    Buttons out{};
    out.Assign(Button::Up,remote&Up);out.Assign(Button::Down,remote&Down);
    out.Assign(Button::Left,remote&Left);out.Assign(Button::Right,remote&Right);
    out.Assign(Button::A,remote&Confirm);out.Assign(Button::B,remote&Back);
    out.Assign(Button::Home,remote&Home);out.Assign(Button::Plus,remote&Menu);
    return out;
}
}
