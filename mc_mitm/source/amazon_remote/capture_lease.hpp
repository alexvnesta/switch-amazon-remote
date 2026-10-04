// Independent one-shot diagnostic capture lifetime. GPL-2.0-only.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace amazon_remote {
// Unavailable is reserved for an adapter whose module configuration disables
// capture. The lease itself returns only Available/Active/Expired/Ended.
enum class CaptureLeaseState : std::uint32_t { Available = 0, Active = 1, Expired = 2, Ended = 3, Unavailable = 4 };
inline constexpr std::uint32_t CaptureLeaseMagic = 0x41524c53u, CaptureLeaseVersion = 1;
struct CaptureLeaseWire {
    std::uint32_t magic{}, version{}, state{}, reserved{};
    std::uint64_t now_boot_ms{}, deadline_boot_ms{};
};
static_assert(sizeof(CaptureLeaseWire) == 32 && offsetof(CaptureLeaseWire, now_boot_ms) == 16 &&
              offsetof(CaptureLeaseWire, deadline_boot_ms) == 24);
static_assert(std::is_trivially_copyable_v<CaptureLeaseWire> && std::is_standard_layout_v<CaptureLeaseWire>);

// A bounded diagnostic permission window, not pairing or output authorization.
// One object lasts one boot. No reset/rearm API. All callers use the same
// monotonic boot-millisecond clock; zero/pre-arm samples are inactive, but may
// simply have been sampled before a concurrently published Arm.
class OneShotCaptureLease {
public:
    static constexpr std::uint64_t DurationMilliseconds = 60000;
    bool Arm(std::uint64_t now_boot_ms) {
        auto expected = Available;
        if (!state_.compare_exchange_strong(expected, Arming, std::memory_order_acq_rel)) return false;
        // Taking the one-shot opportunity consumes it even for an invalid time.
        if (!now_boot_ms || now_boot_ms > std::numeric_limits<std::uint64_t>::max() - DurationMilliseconds) {
            expected = Arming;
            state_.compare_exchange_strong(expected, Ended, std::memory_order_acq_rel);
            return false;
        }
        deadline_.store(now_boot_ms + DurationMilliseconds, std::memory_order_release);
        expected = Arming;
        // End may cancel before or after publication. Never resurrect it with an
        // unconditional Active store. A winning cancellation is permanent.
        return state_.compare_exchange_strong(expected, Live, std::memory_order_acq_rel);
    }
    void End() { state_.exchange(Ended, std::memory_order_acq_rel); }
    bool Active(std::uint64_t now_boot_ms) {
        const auto view = ReadStatus(now_boot_ms);
        if (view.state == static_cast<std::uint32_t>(CaptureLeaseState::Expired) &&
            view.deadline_boot_ms && now_boot_ms >= view.deadline_boot_ms) {
            auto expected = Live;
            state_.compare_exchange_strong(expected, Expired, std::memory_order_acq_rel);
            return false;
        }
        // A clock sampled before another thread's Arm must not poison that new
        // lease. Only reaching its fixed deadline can commit terminal expiry.
        return view.state == static_cast<std::uint32_t>(CaptureLeaseState::Active) &&
               state_.load(std::memory_order_acquire) == Live;
    }
    // Read-only status derives expiry without extending the lease or changing
    // its deadline. Arming is transiently reported Available but never Active;
    // another Arm still fails its CAS. At most two attempts, no retry loop.
    CaptureLeaseWire ReadStatus(std::uint64_t now_boot_ms) const {
        CaptureLeaseWire wire{CaptureLeaseMagic, CaptureLeaseVersion,
            static_cast<std::uint32_t>(CaptureLeaseState::Ended), 0, now_boot_ms, 0};
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            const auto before = state_.load(std::memory_order_acquire);
            // A cancelled Arm can still publish a deadline after End. Do not
            // expose that irrelevant value for Available/Arming/Ended.
            const auto deadline = (before == Live || before == Expired) ? deadline_.load(std::memory_order_acquire) : 0;
            if (state_.load(std::memory_order_acquire) != before) continue;
            if (before == Available || before == Arming) wire.state = static_cast<std::uint32_t>(CaptureLeaseState::Available);
            else if (before == Ended) wire.state = static_cast<std::uint32_t>(CaptureLeaseState::Ended);
            else if (before == Live || before == Expired) {
                wire.deadline_boot_ms = deadline;
                const bool valid = deadline > DurationMilliseconds && now_boot_ms &&
                    now_boot_ms >= deadline - DurationMilliseconds && now_boot_ms < deadline;
                wire.state = static_cast<std::uint32_t>(before == Live && valid ? CaptureLeaseState::Active : CaptureLeaseState::Expired);
            }
            return wire;
        }
        return wire; // Changing state during both attempts: inactive fail-closed.
    }
    CaptureLeaseState Status(std::uint64_t now_boot_ms) const {
        return static_cast<CaptureLeaseState>(ReadStatus(now_boot_ms).state);
    }
private:
    static constexpr std::uint32_t Available = 0, Live = 1, Expired = 2, Ended = 3, Arming = 4;
    std::atomic<std::uint32_t> state_{Available};
    std::atomic<std::uint64_t> deadline_{0};
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
}
