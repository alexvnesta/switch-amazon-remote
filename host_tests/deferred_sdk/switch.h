#pragma once
#include <cstddef>
#include <cstdint>
using Result=std::uint32_t;
struct BtdrvAddress{std::uint8_t address[6];};
extern std::uint64_t test_now_ms;
inline std::uint64_t armGetSystemTick(){return test_now_ms*1000000;}
inline std::uint64_t armTicksToNs(std::uint64_t ticks){return ticks;}
Result btdrvWriteHidData2(BtdrvAddress,const void*,std::size_t);
