// SPDX-License-Identifier: GPL-2.0-only
#ifdef ARMODULE_CONTROLLER_QUERY_Q1
#include "controller_query_runtime.hpp"
#include "controller_query.hpp"
#include <cstring>

// Q1/Q2 WITHDRAWN: the inspected driver patch's legacy custom-command entry
// does not establish the frame expected by the native return path. A host
// worker/timeout cannot contain a crash in that separate process. Do not
// restore transmission based only on firmware version or fake-SDK tests.
// See docs/CONTROLLER_CAPABILITIES_Q2.md. The historical candidate and its
// device reports are preserved separately. No Bluetooth SDK is linked here.
namespace ams::mc::controller_query_runtime {
bool Begin(std::uint32_t) { return false; }
bool Read(void *out, std::size_t size) {
    if (!out || size != sizeof(::amazon_remote::controller_query::Wire)) return false;
    const ::amazon_remote::controller_query::Wire idle{};
    std::memcpy(out, &idle, sizeof idle);
    return true;
}
bool Consume(const void *, std::size_t) { return false; }
// The integration patch consults this for the three generic legacy bridge
// methods. The ordinary saved-peer/navigation methods use a separate route.
bool LegacyBlocked(std::uint16_t) { return true; }
void PowerRequest(std::uint32_t, std::uint32_t) {}
void PowerAcknowledge(std::uint32_t) {}
}
#endif
