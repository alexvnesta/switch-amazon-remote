// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/controller_query.hpp"
#include <array>
#include <cassert>
#include <cstdio>
using namespace amazon_remote::controller_query;
using amazon_remote::controller_capabilities::Outcome;
constexpr auto Op = amazon_remote::controller_capabilities::VendorCapabilities;
std::array<std::uint8_t, 19> Reply() {
    return {1, 0, 0, 0, 0x53, 0xfd, 0, 0, 9, 0, 0, 8, 0, 0x34, 0x12, 16, 1, 12, 0};
}
int main() {
    for (unsigned state=0; state<8; ++state) for (unsigned error=0; error<2; ++error) {
        PowerAdmission p; assert(p.Allowed());
        p.Request(state,error); assert(!p.Allowed());
        p.Acknowledge(0); assert(p.Allowed()==(!error && state<=1));
        p.Request(0,0); assert(!p.Allowed()); p.Acknowledge(1); assert(!p.Allowed());
        p.Request(0,0); p.Acknowledge(0); assert(p.Allowed());
    }
    const auto good = Reply();
    for (bool early : {false, true}) {
        Query q; assert(q.Read(1).phase == Phase::Idle);
        assert(!q.Begin(0, 1) && !q.Begin(1, 0) && !q.Begin(1, UINT64_MAX));
        assert(!q.Consume(good.data(), good.size(), 50));
        assert(q.Begin(22, 100) && q.LegacyBlocked(0));
        assert(!q.Begin(23, 101) && !q.Consume(good.data(), good.size(), 101));
        assert(q.Dispatch(101) && !q.Dispatch(102));
        if (early) assert(q.Consume(good.data(), good.size(), 103));
        q.Submission(0, 104);
        if (!early) assert(q.Consume(good.data(), good.size(), 105));
        const auto w = q.Read(106);
        assert(w.phase == Phase::Response && w.reply.outcome == Outcome::Reported && w.reply.filtering == 1);
        assert(w.reply.max_filters == 12 && w.reply.scan_storage_bytes == 0x1234 && w.reply_count == 1);
        assert(!q.LegacyBlocked(0) && q.LegacyBlocked(Op));
        assert(q.Consume(good.data(), 6, 107) && q.Read(108).late_count == 1);
        assert(q.Read(109).reply == w.reply && !q.Begin(24, 110));
    }
    for (unsigned stage = 0; stage < 5; ++stage) {
        Query q; assert(q.Begin(1, 100));
        if (stage) assert(q.Dispatch(101));
        if (stage >= 2) q.Submission(0, 102);
        if (stage == 3) q.Interrupt(103);
        if (stage == 4) q.Read(99);
        const auto w = q.Read(12100);
        assert(w.phase == (stage >= 3 ? Phase::Interrupted : Phase::TimedOut));
        assert(!q.Dispatch(12101) && !q.Begin(2, 12101));
        assert(q.Consume(good.data(), 6, 12102) == bool(stage));
        q.Submission(0, 12103);
        assert(q.Read(12104).phase == w.phase && q.LegacyBlocked(0) == bool(stage));
    }
    {
        Query q; assert(q.Begin(1, 100)); q.Submission(123, 101);
        assert(q.Read(102).phase == Phase::SubmissionFailed && !q.LegacyBlocked(0));
        assert(!q.Consume(good.data(), good.size(), 103));
    }
    {
        Query q; assert(q.Begin(1, 100) && q.Dispatch(101));
        assert(q.Consume(good.data(), good.size(), 102));
        assert(q.Consume(good.data(), good.size(), 103)); q.Submission(0, 104);
        assert(q.Read(105).phase == Phase::Conflict);
    }
    for (std::size_t n = 0; n < good.size(); ++n) {
        Query q; assert(q.Begin(1, 100) && q.Dispatch(101)); q.Submission(0, 102);
        const bool consumed = q.Consume(good.data(), n, 103);
        assert(consumed == (n >= 6));
        if (consumed) assert(q.Read(104).reply.outcome == Outcome::Malformed);
    }
    for (unsigned status = 1; status <= 255; ++status) {
        auto data = good; data[10] = status;
        Query q; assert(q.Begin(1, 100) && q.Dispatch(101)); q.Submission(0, 102);
        assert(q.Consume(data.data(), data.size(), 103));
        assert(q.Read(104).reply.outcome == Outcome::ControllerError && q.Read(104).reply.status == status);
        data[6] = status;
        Query transport; assert(transport.Begin(1, 100) && transport.Dispatch(101)); transport.Submission(0, 102);
        assert(transport.Consume(data.data(), data.size(), 103));
        assert(transport.Read(104).envelope_status == status && transport.Read(104).reply.outcome == Outcome::NotObserved);
    }
    for (unsigned field : {0u, 1u, 2u, 3u, 4u, 5u}) {
        auto data = good; data[field] ^= 0x80;
        Query q; assert(q.Begin(1, 100) && q.Dispatch(101));
        assert(!q.Consume(data.data(), data.size(), 102));
    }
    std::puts("PASS Q1 query state: both reply orders, failure/timeout/late drain, single use, framing and unrelated events");
}
