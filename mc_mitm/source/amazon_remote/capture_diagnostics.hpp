// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "capture_lease.hpp"

namespace amazon_remote {
enum class CaptureCloseReason : std::uint32_t { None, ExplicitEnd, SleepRequest, ShutdownRequest, PowerError };
// First observed request, not a claim about which concurrent End linearized
// first. Metadata never grants capture, changes its lifetime or rearms it.
struct CaptureCloseSnapshot { CaptureCloseReason reason{}; std::uint64_t boot_ms{}; bool pending{}; };
class CaptureCloseTrace {
public:
    void Record(CaptureCloseReason reason, std::uint64_t boot_ms) {
        if (reason == CaptureCloseReason::None || !boot_ms) return;
        unsigned expected = 0;
        if (!publication_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return;
        reason_ = reason; boot_ms_ = boot_ms;
        publication_.store(2, std::memory_order_release);
    }
    CaptureCloseSnapshot Read() const {
        const auto publication = publication_.load(std::memory_order_acquire);
        if (publication != 2) return {CaptureCloseReason::None, 0, publication == 1};
        return {reason_, boot_ms_, false}; // Immutable after publication.
    }
private:
    std::atomic<unsigned> publication_{};
    CaptureCloseReason reason_{};
    std::uint64_t boot_ms_{};
};
inline constexpr std::uint32_t CaptureDiagnosticMagic = 0x41524332;
inline constexpr std::uint32_t CaptureSnapshotStable = 1, CaptureClosePending = 2, CaptureCloseRecorded = 4;
struct CaptureDiagnosticsWire {
    std::uint32_t magic{}, version{}, state{}, first_close_reason{};
    std::uint64_t now_boot_ms{}, deadline_boot_ms{}, first_close_ms{};
    std::uint32_t flags{}, reserved{};
};
static_assert(sizeof(CaptureDiagnosticsWire) == 48);
static_assert(std::is_trivially_copyable_v<CaptureDiagnosticsWire>);
inline CaptureDiagnosticsWire DescribeCapture(const CaptureLeaseWire &before, const CaptureLeaseWire &after,
                                              const CaptureCloseSnapshot &close, bool configured) {
    CaptureDiagnosticsWire out{CaptureDiagnosticMagic, 1, configured ? after.state : 4u,
        unsigned(close.reason), after.now_boot_ms, configured ? after.deadline_boot_ms : 0, close.boot_ms, 0, 0};
    if (before.state == after.state && before.deadline_boot_ms == after.deadline_boot_ms &&
        before.now_boot_ms == after.now_boot_ms) out.flags |= CaptureSnapshotStable;
    if (close.pending) out.flags |= CaptureClosePending;
    if (close.reason != CaptureCloseReason::None) out.flags |= CaptureCloseRecorded;
    return out;
}
inline bool ValidCaptureDiagnostics(const CaptureDiagnosticsWire &v, std::uint64_t now) {
    if (v.magic != CaptureDiagnosticMagic || v.version != 1 || v.state > 5 || v.first_close_reason > 4 ||
        v.reserved || (v.flags & ~7u) || !v.now_boot_ms || now < v.now_boot_ms || now - v.now_boot_ms > 1500)
        return false;
    const bool recorded = v.flags & CaptureCloseRecorded;
    if (recorded != bool(v.first_close_reason) || recorded != bool(v.first_close_ms) ||
        v.first_close_ms > v.now_boot_ms || ((v.flags & CaptureClosePending) && recorded)) return false;
    if (v.state == 0 || v.state == 3 || v.state == 4 || v.state == 5) return v.deadline_boot_ms == 0;
    if (v.deadline_boot_ms <= OneShotCaptureLease::DurationMilliseconds) return false;
    if (v.state == 2) return v.now_boot_ms >= v.deadline_boot_ms;
    return v.state != 1 || (v.deadline_boot_ms > v.now_boot_ms &&
        v.deadline_boot_ms - v.now_boot_ms <= OneShotCaptureLease::DurationMilliseconds);
}
inline bool CaptureAvailable(const CaptureDiagnosticsWire &v, std::uint64_t now) {
    return ValidCaptureDiagnostics(v, now) && v.state == 0 && v.flags == CaptureSnapshotStable;
}
}
