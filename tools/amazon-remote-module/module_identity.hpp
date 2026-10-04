// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "launch_test.hpp"
#include <cstdio>
namespace armodule {
// Shared exact-size/digest check. This function has no launch/runtime mutations.
inline bool VerifyStartupModule(std::array<std::uint8_t,32> &hash) {
    constexpr char path[]="sdmc:/atmosphere/contents/0100000000a4d001/exefs.nsp";
    constexpr std::size_t expected_size=142150;
    FILE *file=std::fopen(path,"rb"); if (!file) return false;
    Sha256Context sha{}; sha256ContextCreate(&sha);
    std::size_t total=0; unsigned char buffer[4096]{};
    while (total<expected_size) {
        const auto remaining=expected_size-total;
        const auto amount=std::fread(buffer,1,remaining<sizeof buffer?remaining:sizeof buffer,file);
        if (!amount) break;
        sha256ContextUpdate(&sha,buffer,amount); total+=amount;
    }
    const bool exact=total==expected_size && std::fgetc(file)==EOF && !std::ferror(file);
    const bool closed=std::fclose(file)==0;
    if (!exact || !closed) return false;
    sha256ContextGetHash(&sha,hash.data()); return hash==LaunchModuleHash;
}
}
