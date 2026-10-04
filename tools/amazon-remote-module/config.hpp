// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
namespace armodule {
inline bool ParseTarget(const char *text, std::size_t size, std::array<std::uint8_t,6> &out) {
    if (!text || size != 18 || text[17] != '\n') return false;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c-'0';
        if (c >= 'a' && c <= 'f') return c-'a'+10;
        if (c >= 'A' && c <= 'F') return c-'A'+10;
        return -1;
    };
    std::array<std::uint8_t,6> value{};
    unsigned any = 0, all = 255;
    for (unsigned i = 0; i < 6; ++i) {
        const auto high = hex(text[i*3]), low = hex(text[i*3+1]);
        if (high < 0 || low < 0 || (i < 5 && text[i*3+2] != ':')) return false;
        value[i] = high*16+low; any |= value[i]; all &= value[i];
    }
    if (!any || all == 255) return false;
    out = value; return true;
}
}
