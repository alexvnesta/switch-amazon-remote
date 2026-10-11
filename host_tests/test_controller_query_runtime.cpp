// SPDX-License-Identifier: GPL-2.0-only
#define ARMODULE_CONTROLLER_QUERY_Q1
#include "../mc_mitm/source/amazon_remote/controller_query_runtime.cpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

// No fake Bluetooth SDK or transport symbol is provided: a transmission or
// worker dependency would fail to compile/link, not silently become a mock.
int main() {
    namespace rt=ams::mc::controller_query_runtime;
    namespace cq=amazon_remote::controller_query;
    for (const auto nonce : std::array<std::uint32_t,4>{0,1,3,UINT32_MAX}) {
        assert(!rt::Begin(nonce));
        for (const auto state : std::array<std::uint32_t,4>{0,1,2,UINT32_MAX}) {
            rt::PowerRequest(state,0); rt::PowerAcknowledge(0);
            assert(!rt::Begin(nonce));
            rt::PowerRequest(state,91); rt::PowerAcknowledge(93);
            assert(!rt::Begin(nonce));
        }
    }
    for (std::uint32_t op=0;op<=UINT16_MAX;++op)
        assert(rt::LegacyBlocked(static_cast<std::uint16_t>(op)));
    assert(!rt::Read(nullptr,sizeof(cq::Wire)));
    cq::Wire wire{};
    assert(!rt::Read(&wire,sizeof wire-1));
    assert(!rt::Read(&wire,sizeof wire+1));
    assert(!rt::Consume(nullptr,0));
    const std::array<std::uint8_t,19> reply{1,0,0,0,0x53,0xfd,0,0,9,0,0,8,0,0x34,0x12,16,1,12,0};
    assert(!rt::Consume(reply.data(),reply.size()));
    assert(rt::Read(&wire,sizeof wire));
    assert(wire.magic==cq::Magic && wire.version==cq::Version);
    assert(wire.phase==cq::Phase::Idle && !wire.nonce && !wire.issued &&
           !wire.submission_done && !wire.reply_count && !wire.late_count);
    std::puts("PASS withdrawn Q1/Q2 runtime: no transport, no worker, no admission, legacy bridge blocked");
}
