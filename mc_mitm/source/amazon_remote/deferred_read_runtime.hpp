// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ams::mc::deferred_read_runtime {
bool Begin(std::uint64_t nonce);
bool Read(void *out,std::size_t size);
bool Consume(std::uint32_t type,const void *data,std::size_t capacity);
}
