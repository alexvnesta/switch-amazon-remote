#pragma once
#include <functional>
namespace ams::async {using AsyncFunction=std::function<Result()>;bool TryQueueWork(AsyncFunction *function);}
namespace ams::mitm {bool IsInitialized();}
namespace ams::bluetooth::events {bool IsReady();}
