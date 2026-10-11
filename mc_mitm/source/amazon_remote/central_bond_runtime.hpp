// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "central_bond_wire.hpp"
#include <array>
#include <cstddef>
namespace ams::mc::central_bond_runtime {
void Initialize(const std::array<std::uint8_t,6> &peer);
void Poll();
void Suspend();
#ifdef ARMODULE_WAKE_A3
bool PauseSessionForPower();
bool ResumeSessionAfterPower();
bool ReadWakeStatus(void *out,std::size_t size);
bool ResumeSavedSession(std::uint64_t kernel_pid,const ::amazon_remote::central_bond::WakeRequest &request);
#endif
bool PauseUnusedForPower();
bool ResumeUnusedAfterPower();
void ObserveBle(std::uint32_t type,const void *event,std::size_t size);
// Only the privileged core thread, under its event lock. Always consumes/wipes.
void ConsumePrivate(void *event,std::size_t size);
bool Begin(std::uint64_t kernel_pid,const ::amazon_remote::central_bond::Begin &request);
void Cancel(std::uint64_t kernel_pid);
bool ReadStatus(void *out,std::size_t size);
bool PreparePeer(std::uint64_t kernel_pid,const ::amazon_remote::central_bond::PreparePeer &request);
bool ReadPeerStatus(void *out,std::size_t size);
bool ReadHciTrace(void *out,std::size_t size);
}
