// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/control/capture_preflight.hpp"
#include <cassert>
#include <cstring>
#include <thread>
#include <cstdio>
using namespace amazon_remote;
using namespace arsetupview;

int main() {
    OneShotCaptureLease lease;
    CaptureCloseTrace close;
    constexpr std::uint64_t now = 479284;
    const auto before = lease.ReadStatus(now);
    auto wire = DescribeCapture(before, before, close.Read(), true);
    assert(ValidCaptureDiagnostics(wire, now) && CaptureAvailable(wire, now));
    assert(CaptureAvailable(wire, now + 1500) && !CaptureAvailable(wire, now + 1501));
    assert(!CaptureAvailable(wire, now - 1));
    Input idle{}; idle.fresh = idle.process_present = idle.idle_passed = true;
    const auto base = SelectDisconnectCheck(idle);
    assert(!std::strcmp(SelectCapturePreflight(idle, base, wire, 0, now).status, "READY TO SEARCH"));
    close.Record(CaptureCloseReason::SleepRequest, now - 10);
    // A pending close also blocks readiness before its End call executes.
    wire = DescribeCapture(before, before, close.Read(), true);
    assert(!CaptureAvailable(wire, now));
    lease.End();
    const auto ended = lease.ReadStatus(now);
    wire = DescribeCapture(ended, ended, close.Read(), true);
    assert(!std::strcmp(SelectCapturePreflight(idle, base, wire, 0, now).status, "WINDOW CLOSED: SLEEP REQUEST"));
    close.Record(CaptureCloseReason::ExplicitEnd, now);
    assert(close.Read().reason == CaptureCloseReason::SleepRequest && close.Read().boot_ms == now - 10);
    assert(!lease.Arm(now)); // Recording or reading metadata never rearms.
    for (unsigned state = 0; state <= 4; ++state) {
        for (unsigned flags = 0; flags <= 7; ++flags) {
            auto test = wire; test.state = state; test.flags = flags;
            const auto view = SelectCapturePreflight(idle, base, test, 0, now);
            assert(std::strcmp(view.status, "READY TO SEARCH"));
            assert(std::strlen(view.action) < 70 && std::strlen(view.note) < 70);
        }
    }
    auto changing = DescribeCapture(before, ended, {}, true);
    assert(!CaptureAvailable(changing, now));
    assert(!std::strcmp(SelectCapturePreflight(idle, base, changing, 0, now).status, "CHECKING WINDOW"));
    auto pending = DescribeCapture(before, before, {CaptureCloseReason::None, 0, true}, true);
    assert(!CaptureAvailable(pending, now));
    auto unavailable = DescribeCapture(before, before, {}, false);
    assert(!std::strcmp(SelectCapturePreflight(idle, base, unavailable, 0, now).status, "CAPTURE DISABLED"));
    for (auto reason : {CaptureCloseReason::ExplicitEnd, CaptureCloseReason::ShutdownRequest, CaptureCloseReason::None}) {
        auto v = DescribeCapture(ended, ended, {reason, reason == CaptureCloseReason::None ? 0u : now - 1, false}, true);
        const auto displayed = SelectCapturePreflight(idle, base, v, 0, now);
        assert(std::strcmp(displayed.status, "READY TO SEARCH"));
        assert(std::strstr(displayed.status, "EXPLICIT") || std::strstr(displayed.status, "SHUTDOWN") ||
               std::strstr(displayed.status, "UNKNOWN"));
    }
    OneShotCaptureLease timed; assert(timed.Arm(1000));
    auto expired = timed.ReadStatus(61000);
    auto expired_wire = DescribeCapture(expired, expired, {}, true);
    assert(!std::strcmp(SelectCapturePreflight(idle, base, expired_wire, 0, 61000).status, "CAPTURE TIME EXPIRED"));
    auto active = timed.ReadStatus(60999);
    auto active_wire = DescribeCapture(active, active, {}, true);
    assert(!std::strcmp(SelectCapturePreflight(idle, base, active_wire, 0, 60999).status, "CAPTURE ALREADY IN USE"));
    for (unsigned state : {1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
        Input working = idle; working.state = state; working.pad_attached = state == 3;
        const auto original = SelectDisconnectCheck(working);
        assert(!std::strcmp(SelectCapturePreflight(working, original, wire, 1, now).status, original.status));
    }
    Input unstarted{};
    assert(std::strcmp(SelectCapturePreflight(unstarted, Select(unstarted), {}, 1, now).status, "NOT STARTED"));
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        CaptureCloseTrace raced;
        std::thread a([&] { raced.Record(CaptureCloseReason::SleepRequest, 100); });
        std::thread b([&] { raced.Record(CaptureCloseReason::ExplicitEnd, 200); });
        const auto during = raced.Read();
        if (during.reason != CaptureCloseReason::None)
            assert((during.reason == CaptureCloseReason::SleepRequest && during.boot_ms == 100) ||
                   (during.reason == CaptureCloseReason::ExplicitEnd && during.boot_ms == 200));
        a.join(); b.join();
        const auto final = raced.Read();
        assert(!final.pending && ((final.reason == CaptureCloseReason::SleepRequest && final.boot_ms == 100) ||
               (final.reason == CaptureCloseReason::ExplicitEnd && final.boot_ms == 200)));
    }
    armodule::CapturePreflightReport report{}; report.wire = wire; report.now_ms = now;
    report.checksum = armodule::CapturePreflightChecksum(report);
    for (unsigned i = 0; i < 80; ++i) {
        auto bad = report; reinterpret_cast<unsigned char *>(&bad)[i] ^= 1;
        assert(armodule::CapturePreflightChecksum(bad) != report.checksum);
    }
    std::puts("PASS P2: preclosed false-ready regression, stale/future/invalid/unavailable snapshots, first-close retention,1000 concurrent publishers, active/stop UI unchanged,88-byte report checksum; no device operations.");
}
