// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
using u8=std::uint8_t; using u64=std::uint64_t; using s32=std::int32_t; using Result=std::uint32_t;
#define MAKEHOSVERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
struct LoaderModuleInfo {u8 build_id[32];u64 base_address,size;};
std::uint32_t hosversionGet();
u64 armGetSystemTick();
u64 armTicksToNs(u64);
Result pmdmntInitialize();
void pmdmntExit();
Result pmdmntGetProcessId(u64*,u64);
Result ldrDmntInitialize();
void ldrDmntExit();
Result ldrDmntGetProcessModuleInfo(u64,LoaderModuleInfo*,std::size_t,s32*);
