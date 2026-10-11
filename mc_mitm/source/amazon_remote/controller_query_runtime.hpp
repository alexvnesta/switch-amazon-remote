// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ams::mc::controller_query_runtime {
bool Begin(std::uint32_t nonce);
bool Read(void *out, std::size_t size);
bool Consume(const void *event, std::size_t size);
bool LegacyBlocked(std::uint16_t opcode);
void PowerRequest(std::uint32_t state, std::uint32_t result);
void PowerAcknowledge(std::uint32_t result);
}
