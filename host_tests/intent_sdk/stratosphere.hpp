// SPDX-License-Identifier: GPL-2.0-only
// Source-only fixture. No Switch/kernel/Bluetooth implementation.
#pragma once
#include "../filter_sdk/stratosphere.hpp"
namespace ams::os {
struct TimeSpan { std::uint64_t value; std::uint64_t GetMilliSeconds()const{return value;} };
struct SystemTick { TimeSpan ToTimeSpan()const{return {fake_filter::now};} };
inline SystemTick GetSystemTick(){return {};}
}
namespace ams::ncm {
struct SystemProgramId {
    struct Id {std::uint64_t value;};
    static constexpr Id Hid{0x0100000000000013ull}, Btm{0x010000000000002aull};
};
}
