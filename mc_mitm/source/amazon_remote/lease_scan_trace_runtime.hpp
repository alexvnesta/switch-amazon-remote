// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ams::mc::lease_scan_trace {
// Optional passive trace, sharing the existing explicit one-shot capture.
// No radio/client/thread/event reader, forwarding, pairing or lease changes.
void Initialize();
void Arm(std::uint64_t begin_ms);
void ObserveRequest(std::uint32_t command,std::uint64_t program,std::uint64_t process,
                    const void *data,std::size_t size);
void ObserveManaged(std::uint32_t type,const void *data,std::size_t size);
void Poll();
void Suspend();
}
