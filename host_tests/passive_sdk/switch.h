// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
std::uint64_t armGetSystemTick();
inline std::uint64_t armTicksToNs(std::uint64_t value){return value;}
// Intentionally no Bluetooth, service, file or worker declarations.
