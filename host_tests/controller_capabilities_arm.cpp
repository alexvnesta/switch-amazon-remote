// SPDX-License-Identifier: GPL-2.0-only
// Freestanding test exports. Not a firmware patch or a deployment entry point.
#include "../mc_mitm/source/amazon_remote/controller_capabilities.hpp"
using namespace amazon_remote::controller_capabilities;
extern "C" {
bool cc_decode(std::uint16_t opcode, const std::uint8_t *payload, std::size_t length, Reply *out) {
    return Decode(opcode, payload, length, *out);
}
bool cc_start(Observer *observer, std::uint32_t token) { return observer->Start(token); }
bool cc_complete(Observer *observer, std::uint16_t opcode, const std::uint8_t *payload, std::size_t length) {
    return observer->ObserveComplete(opcode, payload, length);
}
bool cc_reset(Observer *observer) { return observer->ObserveResetBoundary(); }
void cc_snapshot(const Observer *observer, Snapshot *out) { *out = observer->ReadSerialized(); }
}
