// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ams::mc::controller_passive_runtime {
bool Consume(std::uint32_t type,const void *event,std::size_t size);
bool Read(void *out,std::size_t size);
}
