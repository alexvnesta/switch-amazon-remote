// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
namespace ardeferredreader {
using BuildId=std::array<std::uint8_t,32>;
// Exact loader metadata/layout check only, not runtime integrity attestation.
template<class Modules> bool MatchedDriver(const Modules &modules,int count,std::uint64_t before,std::uint64_t after,
        const BuildId &main,const BuildId &addon,std::uint64_t main_size,std::uint64_t addon_size=0x3000) {
    if(modules.size()<2 || !before || before!=after || count!=2 ||
       (addon_size!=0x3000 && addon_size!=0x4000))return false;
    return !std::memcmp(modules[0].build_id,main.data(),32) &&
        !std::memcmp(modules[1].build_id,addon.data(),32) && modules[0].base_address &&
        !(modules[0].base_address%4096) && modules[0].size==main_size &&
        modules[0].base_address<=std::numeric_limits<std::uint64_t>::max()-main_size &&
        modules[1].base_address==modules[0].base_address+main_size && modules[1].size==addon_size;
}
template<class Modules> bool MatchedCompanion(const Modules &modules,int count,std::uint64_t before,
        std::uint64_t after,const BuildId &companion) {
    return !modules.empty() && before && before==after && count==1 &&
        !std::memcmp(modules[0].build_id,companion.data(),32);
}
}
