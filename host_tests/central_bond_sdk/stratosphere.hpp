// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <mutex>
namespace ams::os { using SdkMutex=std::mutex; }
namespace central_mock {
template<class F> struct Exit {F f;~Exit(){f();}};
struct MakeExit{};
template<class F> Exit<F> operator+(MakeExit,F f){return {f};}
}
#define CB_JOIN0(a,b) a##b
#define CB_JOIN(a,b) CB_JOIN0(a,b)
#define ON_SCOPE_EXIT auto CB_JOIN(scope_exit_,__LINE__)=central_mock::MakeExit{}+[&]()
