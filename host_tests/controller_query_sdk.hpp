// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <cassert>
namespace query_sdk {
inline bool locked{}, initialized=true, lease_ok=true, create_fail{}, inline_reply{}, interrupt_in_driver{};
inline unsigned driver_calls{};
inline std::uint64_t now=100;
inline std::uint32_t firmware=0x160500, lease=3, driver_result{};
struct Result { std::uint32_t value{}; std::uint32_t GetValue() const{return value;} };
inline void (*worker)(void *){};
}
namespace ams::os {
struct SdkMutex {
    void lock(){assert(!query_sdk::locked);query_sdk::locked=true;}
    void unlock(){assert(query_sdk::locked);query_sdk::locked=false;}
};
struct ThreadType {};
constexpr unsigned ThreadStackAlignment=4096;
inline query_sdk::Result CreateThread(ThreadType *,void (*fn)(void *),void *,void *,std::size_t,int) {
    assert(!query_sdk::locked);
    if(query_sdk::create_fail)return {91};
    query_sdk::worker=fn;return {};
}
inline void SetThreadNamePointer(ThreadType *,const char *){}
inline void StartThread(ThreadType *){assert(!query_sdk::locked);}
}
namespace ams::bluetooth::core {inline bool IsInitialized(){return query_sdk::initialized;}}
namespace ams::mc::amazon_observer {bool ReadCaptureStatus(void *,std::size_t);}
inline std::uint64_t armGetSystemTick(){return query_sdk::now*1000000;}
inline std::uint64_t armTicksToNs(std::uint64_t value){return value;}
inline std::uint32_t hosversionGet(){return query_sdk::firmware;}
#define MAKEHOSVERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define R_FAILED(value) ((value).GetValue()!=0)
#define Module_Libnx 1
#define LibnxError_BadInput 2
#define MAKERESULT(a,b) ((a)|((b)<<9))
std::uint32_t btdrvextSendHciCommand(std::uint16_t,const void *,std::size_t);
