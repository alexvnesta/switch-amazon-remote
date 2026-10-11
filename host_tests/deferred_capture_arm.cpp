// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/driver-capability-p1/deferred_capture.hpp"
namespace d=amazon_remote::deferred_capability;
// Owned storage only in this standalone offline object. NOT a native driver
// address, guessed BSS padding or an instruction to embed/allocate it there.
constinit d::Slot offline_slot;
extern "C" unsigned capture(std::uint16_t opcode,const std::uint8_t *payload,std::uint16_t length){
    return unsigned(offline_slot.TryCapture(opcode,payload,length));
}
extern "C" unsigned read_first(amazon_remote::controller_passive::Event *out){
    if(!out)return 99;
    return unsigned(offline_slot.ReadFirst(*out));
}
