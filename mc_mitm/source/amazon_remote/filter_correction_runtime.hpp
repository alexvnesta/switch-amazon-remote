// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ams::mc::amazon_filter_correction {
// Startup/Main thread only, before any IPC producer; off by default.
void Initialize();
// Existing Main timer only; one exclusive result file after lease ends.
void Poll();
// Existing PSC/Main thread only; prohibits new correction and storage on wake.
void Suspend();
// Existing serialized btdrv IPC server only. A matching request is forwarded
// exactly once using its EXISTING real-driver session, then its result retained.
// All other requests remain SF-original forwards. No new session/thread/read.
using Forward = std::uint32_t (*)(void *,std::uint32_t,const void *,std::size_t);
bool Correct(std::uint32_t command,std::uint64_t program,std::uint64_t process,
             const void *data,std::size_t size,void *context,Forward forward,
             std::uint32_t &result);
}
