// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace ams::mc::sleep_resume_trace {
void Initialize();
void ObserveRequest(std::uint32_t command,std::uint64_t program,const void *data,std::size_t size);
void ObserveBle(std::uint32_t type,const void *data,std::size_t size);
void PowerRequest(std::uint32_t state,std::uint32_t flags,std::uint32_t result);
void PowerAcknowledge(std::uint32_t result);
void Poll(); // Existing Main only; no writes before acknowledged awake state.
}
