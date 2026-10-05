// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Host shim only for the unchanged upstream switch_buttons.hpp. No simulated
// Bluetooth/controller implementation or replacement button enum is supplied.
#include <array>
#include <cstddef>
#include <cstdint>
using u8=std::uint8_t;
using u32=std::uint32_t;
using std::size_t;
#define BIT(n) (1u<<(n))
#define ALWAYS_INLINE inline
