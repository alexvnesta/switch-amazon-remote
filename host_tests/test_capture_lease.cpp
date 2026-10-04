#include "../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include <atomic>
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>

using namespace amazon_remote;
void CheckWire(const CaptureLeaseWire &wire, std::uint64_t now) {
    assert(wire.magic == CaptureLeaseMagic && wire.version == 1 && wire.reserved == 0);
    assert(wire.state <= static_cast<std::uint32_t>(CaptureLeaseState::Ended) && wire.now_boot_ms == now);
    if (wire.state == static_cast<std::uint32_t>(CaptureLeaseState::Available) ||
        wire.state == static_cast<std::uint32_t>(CaptureLeaseState::Ended)) assert(wire.deadline_boot_ms == 0);
    if (wire.state == static_cast<std::uint32_t>(CaptureLeaseState::Active))
        assert(now && wire.deadline_boot_ms > now && now >= wire.deadline_boot_ms - OneShotCaptureLease::DurationMilliseconds);
}
void Boundaries() {
    OneShotCaptureLease lease;
    assert(lease.Status(1) == CaptureLeaseState::Available && !lease.Active(1));
    CheckWire(lease.ReadStatus(1), 1);
    // Arming is independent of the old boot+120s log lifetime.
    assert(lease.Arm(900000)); assert(!lease.Arm(900001));
    auto view = lease.ReadStatus(900000); CheckWire(view, 900000);
    assert(view.deadline_boot_ms == 960000 && lease.Active(900000));
    assert(lease.Active(959999)); assert(lease.Status(960000) == CaptureLeaseState::Expired);
    assert(!lease.Active(960000)); assert(!lease.Active(900001));
    assert(!lease.Arm(970000)); assert(lease.Status(970000) == CaptureLeaseState::Expired);
    lease.End(); assert(lease.Status(1) == CaptureLeaseState::Ended);
    assert(!lease.Active(900001) && !lease.Arm(1000000)); CheckWire(lease.ReadStatus(1000000), 1000000);
    OneShotCaptureLease cancelled; cancelled.End(); cancelled.End();
    assert(!cancelled.Arm(1) && cancelled.Status(1) == CaptureLeaseState::Ended);
    OneShotCaptureLease after_arm; assert(after_arm.Arm(1)); after_arm.End();
    assert(!after_arm.Active(1) && !after_arm.Arm(2)); CheckWire(after_arm.ReadStatus(1), 1);
    for (const auto bad : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max(),
                          std::numeric_limits<std::uint64_t>::max() - 59999}) {
        OneShotCaptureLease invalid; assert(!invalid.Arm(bad));
        assert(invalid.Status(1) == CaptureLeaseState::Ended && !invalid.Arm(1));
    }
    OneShotCaptureLease last; const auto maximum_start = std::numeric_limits<std::uint64_t>::max() - 60000;
    assert(last.Arm(maximum_start)); assert(last.Active(maximum_start));
    assert(last.Active(std::numeric_limits<std::uint64_t>::max() - 1));
    assert(!last.Active(std::numeric_limits<std::uint64_t>::max()));
    for (const auto bad_now : {std::uint64_t{0}, std::uint64_t{999}}) {
        OneShotCaptureLease invalid_clock; assert(invalid_clock.Arm(1000));
        assert(invalid_clock.Status(bad_now) == CaptureLeaseState::Expired);
        assert(!invalid_clock.Active(bad_now)); assert(invalid_clock.Active(1000));
        assert(!invalid_clock.Arm(1000));
    }
    // A read-only future status must not mutate a still-current lease.
    OneShotCaptureLease read_only; assert(read_only.Arm(100));
    assert(read_only.Status(60100) == CaptureLeaseState::Expired && read_only.Active(100));
}
void Races() {
    // Clock acquisition may precede publication of another thread's Arm. A
    // resumed reader with that stale pre-arm sample must not expire the lease.
    for (const auto stale_now : {std::uint64_t{999}, std::uint64_t{0}}) {
        OneShotCaptureLease lease;
        std::atomic<bool> sampled{false}, resume{false};
        std::thread reader([&] {
            const auto now = stale_now;
            sampled.store(true, std::memory_order_release);
            while (!resume.load(std::memory_order_acquire)) {}
            assert(!lease.Active(now));
        });
        while (!sampled.load(std::memory_order_acquire)) {}
        assert(lease.Arm(1000));
        resume.store(true, std::memory_order_release); reader.join();
        assert(lease.Active(1000));
        assert(lease.ReadStatus(1000).deadline_boot_ms == 61000);
        assert(!lease.Active(61000) && !lease.Active(1000));
    }
    unsigned arm_wins = 0;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        OneShotCaptureLease lease; std::atomic<bool> start{false}, armed{false};
        std::thread arm([&] {
            while (!start.load(std::memory_order_acquire)) {}
            armed.store(lease.Arm(1000), std::memory_order_release);
        });
        std::thread end([&] {
            while (!start.load(std::memory_order_acquire)) {}
            for (unsigned i = 0; i < 3; ++i) CheckWire(lease.ReadStatus(1000), 1000);
            lease.End();
            // Cancellation cannot be reversed by the concurrent Arm publisher.
            assert(!lease.Active(1000));
            assert(lease.Status(1000) == CaptureLeaseState::Ended);
            CheckWire(lease.ReadStatus(1000), 1000);
        });
        start.store(true, std::memory_order_release); arm.join(); end.join();
        arm_wins += armed.load(std::memory_order_acquire);
        assert(!lease.Active(1000) && !lease.Arm(1000));
        assert(lease.Status(1000) == CaptureLeaseState::Ended);
    }
    // Concurrent expiry/status/cancellation never changes or extends the deadline.
    OneShotCaptureLease lease; assert(lease.Arm(1000)); std::atomic<bool> start{false};
    std::thread reader([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (unsigned i = 0; i < 10000; ++i) {
            auto wire = lease.ReadStatus(60999); CheckWire(wire, 60999);
            if (wire.deadline_boot_ms) assert(wire.deadline_boot_ms == 61000);
            assert(!lease.Arm(1000));
        }
    });
    std::thread expiry([&] {
        while (!start.load(std::memory_order_acquire)) {}
        assert(!lease.Active(61000)); lease.End();
    });
    start.store(true, std::memory_order_release); reader.join(); expiry.join();
    assert(lease.Status(1000) == CaptureLeaseState::Ended && !lease.Active(1000));
    std::cout << "PASS10000 concurrent Arm/End races; successful Arm before cancellation=" << arm_wins << '\n';
}
int main() {
    Boundaries(); Races();
    std::cout << "PASS one-shot60s lease, delayed Arm, exact deadline, invalid clocks/overflow, read-only32B status and permanent End\n";
}
