// SPDX-License-Identifier: GPL-2.0-only
#include "../control/operator_input.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using arsetupinput::OperatorInput;
    using arsetupinput::Frame;
    static_assert(arsetupinput::SupportedPlayers == 8 && arsetupinput::InputSources == 9);
    constexpr auto A = 1ull, Plus = 1ull << 10, ZL = 1ull << 8, X = 1ull << 2;
    for (unsigned slot = 0; slot < 9; ++slot) {
        for (unsigned style : {2u, 4u, 8u, 16u, 0x1eu}) {
            OperatorInput input;
            Frame pads{};
            pads[slot] = {style, 1u << slot, A};
            assert(!input.Update(pads).available); // Held entry cannot launch.
            pads[slot].held = 0;
            assert(input.Update(pads).available);
            for (auto buttons : {A, X, Plus, ZL}) {
                pads[slot].held = buttons;
                assert(input.Update(pads).down == buttons);
                assert(input.Update(pads).down == 0);
                pads[slot].held = 0;
                assert(input.Update(pads).down == 0);
            }
        }
        for (unsigned style : {0u, 1u, 3u, 5u, 0x1fu, 0x20u, 0xffffffffu}) {
            OperatorInput input;
            Frame pads{};
            for (auto buttons : {0ull, A, Plus, ZL, X}) {
                pads[slot] = {style, 1u << slot, buttons};
                const auto sample = input.Update(pads);
                assert(!sample.available && !sample.held && !sample.down && !sample.sources);
            }
        }
    }
    OperatorInput input;
    Frame pads{};
    pads[0] = {4, 1, 0};
    assert(input.Update(pads).available);
    pads[0].held = A;
    assert(input.Update(pads).down == A);
    // Exact user regression: remote becomes P1, right Joy-Con moves to P2.
    pads[0] = {1, 1, A | Plus | X | ZL};
    pads[1] = {16, 2, X};
    assert(!input.Update(pads).available); // Release once after reassignment.
    pads[1].held = 0;
    assert(input.Update(pads).available); // Remote held buttons do not block.
    pads[1].held = X;
    auto sample = input.Update(pads);
    assert(sample.down == X && sample.held == X && sample.sources == 2);
    pads[1].held = 0; input.Update(pads);
    pads[1].held = Plus;
    assert(input.Update(pads).down == Plus);
    pads[1].held = 0; input.Update(pads);
    pads[0].held = 0; assert(input.Update(pads).down == 0);
    pads[0].held = A | X | ZL | Plus;
    assert(input.Update(pads).down == 0); // Remote never controls Setup.

    // Handheld remains usable with FullKey P1. Left and right may be split.
    pads[1] = {}; pads[8] = {2, 256, X};
    assert(!input.Update(pads).available);
    pads[8].held = 0; assert(input.Update(pads).available);
    pads[8].held = X; assert(input.Update(pads).down == X);
    pads[8] = {}; pads[1] = {8, 2, 0}; pads[2] = {16, 4, 0};
    assert(input.Update(pads).available);
    pads[1].held = ZL; assert(input.Update(pads).down == ZL);
    pads[1].held = 0; input.Update(pads);
    pads[2].held = X; assert(input.Update(pads).down == X);
    // Individual styles changing must invalidate even an unchanged OR mask.
    pads[1].styles = 16; pads[2].styles = 8;
    assert(!input.Update(pads).available);
    pads[2].held = 0; assert(input.Update(pads).available);
    pads[2].held = X; assert(input.Update(pads).down == X);

    // Missing/aliased/merged source IDs cannot smuggle a slot into another.
    for (unsigned mask : {0u, 1u, 3u, 0x101u, 512u, 0xffffffffu}) {
        OperatorInput bad;
        Frame malformed{};
        malformed[1] = {4, mask, 0};
        assert(!bad.Update(malformed).available);
    }
    pads = {}; assert(!input.Update(pads).available);
    pads[7] = {16, 128, X}; assert(!input.Update(pads).available);
    pads[7].held = 0; assert(input.Update(pads).available);
    pads[7].held = X; assert(input.Update(pads).down == X);
    std::puts("PASS P4: all 8 players plus handheld; P1 remote/P2 Joy-Con X and + regression; split Joy-Cons; FullKey exclusion; source/style reassignment and neutral-release guards. HOST MODEL ONLY.");
}
