// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>

namespace arsetupinput {
inline constexpr unsigned SupportedPlayers = 8;
inline constexpr unsigned InputSources = SupportedPlayers + 1; // Plus handheld.
inline constexpr std::uint32_t JoyConStyles = 0x1e; // Handheld, Dual, Left, Right.
struct PadSample { std::uint32_t styles{}; unsigned sources{}; std::uint64_t held{}; };
using Frame = std::array<PadSample, InputSources>;
struct Sample {
    bool available{};
    std::uint64_t held{}, down{};
    std::uint32_t styles{};
    unsigned sources{};
};

// Supported slots and operator input are independent. The virtual remote is
// FullKey and must never issue Setup commands, including after reassignment.
// Filter each slot BEFORE combining input. The default player-1/handheld PadState
// misses a reassigned Joy-Con and can mix the remote's style into handheld input.
// This is a style filter for this diagnostic, not controller authentication.
class OperatorInput {
public:
    Sample Update(const Frame &pads) {
        std::uint64_t layout{}, held{};
        std::uint32_t styles{};
        unsigned sources{};
        for (unsigned i = 0; i < InputSources; ++i) {
            const auto &pad = pads[i];
            if (pad.sources != (1u << i) || !pad.styles || (pad.styles & ~JoyConStyles)) continue;
            // Four bits per slot preserve its individual style across reassignment.
            layout |= std::uint64_t(pad.styles >> 1) << (4 * i);
            styles |= pad.styles;
            sources |= pad.sources;
            held |= pad.held;
        }
        if (!sources) {
            layout_ = previous_ = 0; released_ = false;
            return {};
        }
        if (layout != layout_) {
            layout_ = layout; previous_ = held; released_ = false;
        }
        if (!released_) {
            previous_ = held;
            if (!held) released_ = true;
            return {released_, 0, 0, styles, sources};
        }
        const auto down = held & ~previous_;
        previous_ = held;
        return {true, held, down, styles, sources};
    }
private:
    std::uint64_t layout_{}, previous_{};
    bool released_{};
};
}
