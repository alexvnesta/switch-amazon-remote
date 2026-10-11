#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
using Result=std::uint32_t;
namespace ams {using Result=::Result;namespace os {
struct SdkMutex {
    void lock(){assert(!locked);locked=true;}
    void unlock(){assert(locked);locked=false;}
    bool locked{};
};
}}
#define R_SUCCEED() return 0
