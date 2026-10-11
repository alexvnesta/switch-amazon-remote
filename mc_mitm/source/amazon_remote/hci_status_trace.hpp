// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "central_bond_wire.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
namespace amazon_remote::central_bond {
// Caller serializes access and supplies independently validated capture scope.
inline bool CaptureHciStatus(HciTrace &out,std::span<const std::uint8_t> bytes,
        const std::array<std::uint8_t,6> &peer,std::uint32_t nonce,
        std::uint64_t start,std::uint64_t now,bool active) {
    if(!active || !out.supported || !nonce || !start || now<start ||
       bytes.size()<64 || out.count>4 || bytes[10]!=6 || bytes[16]>3)return false;
    std::uint32_t magic{},opcode{};
    std::memcpy(&magic,bytes.data(),4);std::memcpy(&opcode,bytes.data()+20,4);
    if(magic!=Magic || opcode!=0x200d || !std::equal(peer.begin(),peer.end(),bytes.begin()+4) ||
       !std::all_of(bytes.begin()+12,bytes.begin()+16,[](auto b){return b==0;}) ||
       !std::all_of(bytes.begin()+17,bytes.begin()+20,[](auto b){return b==0;}) ||
       !std::all_of(bytes.begin()+24,bytes.begin()+64,[](auto b){return b==0;}))return false;
    if(out.window_nonce && (out.window_nonce!=nonce || out.capture_start_ms!=start))return false;
    if(out.count && now<out.entries[out.count-1].boot_ms)return false;
    out.window_nonce=nonce;out.capture_start_ms=start;
    if(out.count==4) { if(out.dropped<std::numeric_limits<std::uint32_t>::max())++out.dropped;return true; }
    out.entries[out.count++]={now,bytes[11],bytes[16]};return true;
}
}
